#include "../base/SRC_FIRST.hpp"

#include "../std/bind.hpp"
#include "../std/set.hpp"
#include "../std/algorithm.hpp"

#include "../yg/screen.hpp"
#include "../yg/base_texture.hpp"

#include "screen_coverage.hpp"
#include "tile_renderer.hpp"
#include "window_handle.hpp"
#include "coverage_generator.hpp"

ScreenCoverage::ScreenCoverage()
  : m_tiler(0, 0)
{}

ScreenCoverage::ScreenCoverage(TileRenderer * tileRenderer,
                               CoverageGenerator * coverageGenerator,
                               size_t tileSize,
                               size_t scaleEtalonSize)
  : m_tileRenderer(tileRenderer),
    m_tiler(tileSize, scaleEtalonSize),
    m_coverageGenerator(coverageGenerator)
{}

ScreenCoverage * ScreenCoverage::Clone()
{
  ScreenCoverage * res = new ScreenCoverage();

  res->m_tileRenderer = m_tileRenderer;
  res->m_tiler = m_tiler;
  res->m_screen = m_screen;
  res->m_coverageGenerator = m_coverageGenerator;
  res->m_tileRects = m_tileRects;

  TileCache * tileCache = &m_tileRenderer->GetTileCache();

  tileCache->writeLock();

  res->m_tiles = m_tiles;

  for (TileSet::const_iterator it = res->m_tiles.begin(); it != res->m_tiles.end(); ++it)
  {
    Tiler::RectInfo const & ri = (*it)->m_rectInfo;
    tileCache->lockTile(ri);
  }

  tileCache->writeUnlock();

  res->m_infoLayer = m_infoLayer;

  return res;
}

void ScreenCoverage::Merge(Tiler::RectInfo const & ri)
{
  if (m_tileRects.find(ri) == m_tileRects.end())
    return;

  TileCache * tileCache = &m_tileRenderer->GetTileCache();

  Tile const * tile = 0;
  bool hasTile = false;

  tileCache->readLock();

  hasTile = tileCache->hasTile(ri);
  if (hasTile)
    tile = &tileCache->getTile(ri);

  bool addTile = false;

  if (hasTile)
  {
    addTile = (m_tiles.find(tile) == m_tiles.end());

    if (addTile)
    {
       tileCache->lockTile(ri);
       m_tiles.insert(tile);
       m_tileRects.erase(ri);
    }
  }

  tileCache->readUnlock();

  if (addTile)
    m_infoLayer.merge(*tile->m_infoLayer.get(),
                       tile->m_tileScreen.PtoGMatrix() * m_screen.GtoPMatrix());

}

void ScreenCoverage::Remove(Tile const * tile)
{
}

bool LessRectInfo::operator()(Tile const * l, Tile const * r) const
{
  return l->m_rectInfo.toUInt64Cell() < r->m_rectInfo.toUInt64Cell();
}

void ScreenCoverage::SetScreen(ScreenBase const & screen, bool mergePathNames)
{
  m_screen = screen;

  m_tiler.seed(m_screen, m_screen.GlobalRect().Center());

  vector<Tiler::RectInfo> allRects;
  vector<Tiler::RectInfo> newRects;
  TileSet tiles;

  m_tiler.visibleTiles(allRects);

  TileCache * tileCache = &m_tileRenderer->GetTileCache();

  tileCache->writeLock();

  double drawScale = -1;

  for (unsigned i = 0; i < allRects.size(); ++i)
    if (drawScale == -1)
      drawScale = allRects[i].m_drawScale;
    else
      ASSERT(drawScale == allRects[i].m_drawScale, (drawScale, allRects[i].m_drawScale));

  for (unsigned i = 0; i < allRects.size(); ++i)
  {
    m_tileRects.insert(allRects[i]);
    Tiler::RectInfo ri = allRects[i];
    if (tileCache->hasTile(ri))
    {
      tileCache->touchTile(ri);
      Tile const * tile = &tileCache->getTile(ri);
      ASSERT(tiles.find(tile) == tiles.end(), ());
      tiles.insert(tile);
    }
    else
      newRects.push_back(ri);
  }

  tileCache->writeUnlock();

  /// computing difference between current and previous coverage
  /// tiles, that aren't in current coverage are unlocked to allow their deletion from TileCache
  /// tiles, that are new to the current coverage are added into m_tiles and locked in TileCache

  TileSet erasedTiles;
  TileSet addedTiles;

  set_difference(m_tiles.begin(), m_tiles.end(), tiles.begin(), tiles.end(), inserter(erasedTiles, erasedTiles.end()));
  set_difference(tiles.begin(), tiles.end(), m_tiles.begin(), m_tiles.end(), inserter(addedTiles, addedTiles.end()));

  tileCache->readLock();

  for (TileSet::const_iterator it = erasedTiles.begin(); it != erasedTiles.end(); ++it)
  {
    Tiler::RectInfo ri = (*it)->m_rectInfo;
    tileCache->unlockTile((*it)->m_rectInfo);
    /// here we should "unmerge" erasedTiles[i].m_infoLayer from m_infoLayer
  }

  for (TileSet::const_iterator it = addedTiles.begin(); it != addedTiles.end(); ++it)
  {
    Tiler::RectInfo ri = (*it)->m_rectInfo;
    tileCache->lockTile((*it)->m_rectInfo);
//    m_infoLayer.merge(*((*it)->m_infoLayer.get()), (*it)->m_tileScreen.PtoGMatrix() * screen.GtoPMatrix());
  }

  m_infoLayer.clear();
  for (TileSet::const_iterator it = m_tiles.begin(); it != m_tiles.end(); ++it)
    m_infoLayer.merge(*((*it)->m_infoLayer.get()), (*it)->m_tileScreen.PtoGMatrix() * screen.GtoPMatrix());

  tileCache->readUnlock();

  m_tiles = tiles;

  /// adding commands for tiles which aren't in cache
  for (size_t i = 0; i < newRects.size(); ++i)
  {
    m_tileRenderer->AddCommand(newRects[i], m_tiler.sequenceID(),
                               bind(&CoverageGenerator::AddMergeTileTask, m_coverageGenerator, newRects[i]));
  }
}

ScreenCoverage::~ScreenCoverage()
{
  TileCache * tileCache = &m_tileRenderer->GetTileCache();

  tileCache->writeLock();
  for (TileSet::const_iterator it = m_tiles.begin(); it != m_tiles.end(); ++it)
  {
    Tiler::RectInfo const & ri = (*it)->m_rectInfo;
    tileCache->unlockTile(ri);
  }

  tileCache->writeUnlock();

  m_tiles.clear();
  m_infoLayer.clear();
}

void ScreenCoverage::Draw(yg::gl::Screen * s, ScreenBase const & screen)
{
  for (TileSet::const_iterator it = m_tiles.begin(); it != m_tiles.end(); ++it)
  {
    Tile const * tile = *it;
    size_t tileWidth = tile->m_renderTarget->width();
    size_t tileHeight = tile->m_renderTarget->height();

    s->blit(tile->m_renderTarget, tile->m_tileScreen, screen, true,
            yg::Color(),
            m2::RectI(0, 0, tileWidth - 2, tileHeight - 2),
            m2::RectU(1, 1, tileWidth - 1, tileHeight - 1));

  }

  m_infoLayer.draw(s, m_screen.PtoGMatrix() * screen.GtoPMatrix());
}
