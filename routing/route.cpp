#include "routing/route.hpp"

#include "indexer/mercator.hpp"

#include "platform/location.hpp"

#include "geometry/angles.hpp"
#include "geometry/point2d.hpp"

#include "base/logging.hpp"

#include "std/numeric.hpp"

#include "map/location_state.hpp"


namespace routing
{

static double const LOCATION_TIME_THRESHOLD = 60.0*1.0;
static double const ON_ROAD_TOLERANCE_M = 50.0;
static double const ON_END_TOLERANCE_M = 10.0;

string DebugPrint(TurnItem const & turnItem)
{
  stringstream out;
  out << "[ TurnItem: m_index = " << turnItem.m_index
      << ", m_turn = " << DebugPrint(turnItem.m_turn)
      << ", m_lanes = " << ::DebugPrint(turnItem.m_lanes) << ", m_exitNum = " << turnItem.m_exitNum
      << ", m_sourceName = " << turnItem.m_sourceName
      << ", m_targetName = " << turnItem.m_targetName
      << ", m_keepAnyway = " << turnItem.m_keepAnyway << " ]" << endl;
  return out.str();
}


Route::Route(string const & router, vector<m2::PointD> const & points, string const & name)
  : m_router(router), m_poly(points), m_name(name)
{
  Update();
}

void Route::Swap(Route & rhs)
{
  m_router.swap(rhs.m_router);
  m_poly.Swap(rhs.m_poly);
  m_name.swap(rhs.m_name);
  m_segDistance.swap(rhs.m_segDistance);
  m_segProj.swap(rhs.m_segProj);
  swap(m_current, rhs.m_current);
  swap(m_currentTime, rhs.m_currentTime);
  swap(m_turns, rhs.m_turns);
  swap(m_times, rhs.m_times);
  m_turnsGeom.swap(rhs.m_turnsGeom);
  m_absentCountries.swap(rhs.m_absentCountries);
}

void Route::SetTurnInstructions(TurnsT & v)
{
  swap(m_turns, v);
}

void Route::SetSectionTimes(TimesT & v)
{
  swap(m_times, v);
}

void Route::SetTurnInstructionsGeometry(turns::TurnsGeomT & v)
{
  swap(m_turnsGeom, v);
}

double Route::GetDistance() const
{
  ASSERT(!m_segDistance.empty(), ());
  return m_segDistance.back();
}

double Route::GetCurrentDistanceFromBegin() const
{
  ASSERT(m_current.IsValid(), ());

  return ((m_current.m_ind > 0 ? m_segDistance[m_current.m_ind - 1] : 0.0) +
          MercatorBounds::DistanceOnEarth(m_poly.GetPoint(m_current.m_ind), m_current.m_pt));
}

double Route::GetCurrentDistanceToEnd() const
{
  ASSERT(m_current.IsValid(), ());
  ASSERT_LESS(m_current.m_ind, m_segDistance.size(), ());

  return (m_segDistance.back() - m_segDistance[m_current.m_ind] +
          MercatorBounds::DistanceOnEarth(m_current.m_pt, m_poly.GetPoint(m_current.m_ind + 1)));
}

uint32_t Route::GetAllTime() const
{
  return m_times.empty() ? 0 : m_times.back().second;
}

uint32_t Route::GetTime() const
{
  size_t const polySz = m_poly.GetSize();
  if (m_times.empty() || m_poly.GetSize() == 0)
  {
    ASSERT(!m_times.empty(), ());
    ASSERT(polySz != 0, ());
    return 0;
  }

  TimesT::const_iterator it = upper_bound(m_times.begin(), m_times.end(), m_current.m_ind,
                                         [](size_t v, Route::TimeItemT const & item) { return v < item.first; });

  if (it == m_times.end())
    return 0;

  size_t idx = distance(m_times.begin(), it);
  double time = (*it).second;
  if (idx > 0)
    time -= m_times[idx - 1].second;

  auto distFn = [&](uint32_t start, uint32_t end)
  {
    if (start > polySz || end > polySz)
    {
      ASSERT(false, ());
      return 0.;
    }
    double d = 0.0;
    for (uint32_t i = start + 1; i < end; ++i)
      d += MercatorBounds::DistanceOnEarth(m_poly.GetPoint(i - 1), m_poly.GetPoint(i));
    return d;
  };

  ASSERT_LESS_OR_EQUAL(m_times[idx].first, m_poly.GetSize(), ());
  double const dist = distFn(idx > 0 ? m_times[idx - 1].first : 0, m_times[idx].first + 1);

  if (!my::AlmostEqualULPs(dist, 0.))
  {
    double const distRemain = distFn(m_current.m_ind, m_times[idx].first + 1) -
                        MercatorBounds::DistanceOnEarth(m_current.m_pt, m_poly.GetPoint(m_current.m_ind));
    return (uint32_t)((GetAllTime() - (*it).second) + (double)time * (distRemain / dist));
  }
  else
    return (uint32_t)((GetAllTime() - (*it).second));
}

void Route::GetTurn(double & distance, TurnItem & turn) const
{
  if (m_segDistance.empty() || m_turns.empty())
  {
    ASSERT(!m_segDistance.empty(), ());
    ASSERT(!m_turns.empty(), ());
    distance = 0;
    turn = TurnItem();
    return;
  }

  TurnItem t;
  t.m_index = m_current.m_ind;
  auto it = upper_bound(m_turns.begin(), m_turns.end(), t,
            [](TurnItem const & v, TurnItem const & item)
            {
              return v.m_index < item.m_index;
            });

  ASSERT_GREATER_OR_EQUAL((*it).m_index - 1, 0, ());

  size_t const segIdx = (*it).m_index - 1;
  turn = (*it);
  distance = m_segDistance[segIdx] - GetCurrentDistanceFromBegin();
}

bool Route::MoveIterator(location::GpsInfo const & info) const
{
  double predictDistance = -1.0;
  if (m_currentTime > 0.0 && info.HasSpeed())
  {
    /// @todo Need to distinguish GPS and WiFi locations.
    /// They may have different time metrics in case of incorrect system time on a device.
    double const deltaT = info.m_timestamp - m_currentTime;
    if (deltaT > 0.0 && deltaT < LOCATION_TIME_THRESHOLD)
      predictDistance = info.m_speed * deltaT;
  }

  m2::RectD const rect = MercatorBounds::MetresToXY(
        info.m_longitude, info.m_latitude,
        max(ON_ROAD_TOLERANCE_M, info.m_horizontalAccuracy));
  IterT const res = FindProjection(rect, predictDistance);
  if (res.IsValid())
  {
    m_current = res;
    m_currentTime = info.m_timestamp;
    return true;
  }
  else
    return false;
}

double Route::GetCurrentSqDistance(m2::PointD const & pt) const
{
  ASSERT(m_current.IsValid(), ());
  return pt.SquareLength(m_current.m_pt);
}

double Route::GetPolySegAngle(size_t ind) const
{
  size_t const polySz = m_poly.GetSize();

  if (ind + 1 >= polySz)
  {
    ASSERT(false, ());
    return 0;
  }

  m2::PointD const p1 = m_poly.GetPoint(ind);
  m2::PointD p2;
  size_t i = ind + 1;
  do
  {
    p2 = m_poly.GetPoint(i);
  }
  while (m2::AlmostEqualULPs(p1, p2) && ++i < polySz);
  return (i == polySz) ? 0 : my::RadToDeg(ang::AngleTo(p1, p2));
}

void Route::MatchLocationToRoute(location::GpsInfo & location, location::RouteMatchingInfo & routeMatchingInfo) const
{
  if (m_current.IsValid())
  {
    const m2::PointD locationMerc(MercatorBounds::LonToX(location.m_longitude),
                                  MercatorBounds::LatToY(location.m_latitude));
    const double distFromRoute = MercatorBounds::DistanceOnEarth(m_current.m_pt, locationMerc);
    if (distFromRoute < ON_ROAD_TOLERANCE_M)
    {
      location.m_latitude = MercatorBounds::YToLat(m_current.m_pt.y);
      location.m_longitude = MercatorBounds::XToLon(m_current.m_pt.x);
      location.m_bearing = location::AngleToBearing(GetPolySegAngle(m_current.m_ind));
      routeMatchingInfo.Set(m_current.m_pt, m_current.m_ind);
    }
  }
}

bool Route::IsCurrentOnEnd() const
{
  return (GetCurrentDistanceToEnd() < ON_END_TOLERANCE_M);
}

Route::IterT Route::FindProjection(m2::RectD const & posRect, double predictDistance) const
{
  ASSERT(m_current.IsValid(), ());
  ASSERT_LESS(m_current.m_ind, m_poly.GetSize() - 1, ());

  IterT res;
  if (predictDistance >= 0.0)
  {
    res = GetClosestProjection(posRect, [&] (IterT const & it)
    {
      return fabs(GetDistanceOnPolyline(m_current, it) - predictDistance);
    });
  }
  else
  {
    m2::PointD const currPos = posRect.Center();
    res = GetClosestProjection(posRect, [&] (IterT const & it)
    {
      return MercatorBounds::DistanceOnEarth(it.m_pt, currPos);
    });
  }

  return res;
}

double Route::GetDistanceOnPolyline(IterT const & it1, IterT const & it2) const
{
  ASSERT(it1.IsValid() && it2.IsValid(), ());
  ASSERT_LESS_OR_EQUAL(it1.m_ind, it2.m_ind, ());
  ASSERT_LESS(it1.m_ind, m_poly.GetSize(), ());
  ASSERT_LESS(it2.m_ind, m_poly.GetSize(), ());

  if (it1.m_ind == it2.m_ind)
    return MercatorBounds::DistanceOnEarth(it1.m_pt, it2.m_pt);

  return (MercatorBounds::DistanceOnEarth(it1.m_pt, m_poly.GetPoint(it1.m_ind + 1)) +
          m_segDistance[it2.m_ind - 1] - m_segDistance[it1.m_ind] +
          MercatorBounds::DistanceOnEarth(m_poly.GetPoint(it2.m_ind), it2.m_pt));
}

void Route::Update()
{
  size_t n = m_poly.GetSize();
  ASSERT_GREATER(n, 1, ());
  --n;

  m_segDistance.resize(n);
  m_segProj.resize(n);

  double dist = 0.0;
  for (size_t i = 0; i < n; ++i)
  {
    m2::PointD const & p1 = m_poly.GetPoint(i);
    m2::PointD const & p2 = m_poly.GetPoint(i + 1);

    dist += MercatorBounds::DistanceOnEarth(p1, p2);

    m_segDistance[i] = dist;
    m_segProj[i].SetBounds(p1, p2);
  }

  m_current = IterT(m_poly.Front(), 0);
  m_currentTime = 0.0;
}

template <class DistanceF>
Route::IterT Route::GetClosestProjection(m2::RectD const & posRect, DistanceF const & distFn) const
{
  IterT res;
  double minDist = numeric_limits<double>::max();

  m2::PointD const currPos = posRect.Center();
  size_t const count = m_poly.GetSize() - 1;
  for (size_t i = m_current.m_ind; i < count; ++i)
  {
    m2::PointD const pt = m_segProj[i](currPos);

    if (!posRect.IsPointInside(pt))
      continue;

    IterT it(pt, i);
    double const dp = distFn(it);
    if (dp < minDist)
    {
      res = it;
      minDist = dp;
    }
  }

  return res;
}

string DebugPrint(Route const & r)
{
  return DebugPrint(r.m_poly);
}

} // namespace routing
