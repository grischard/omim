#include "storage/country.hpp"

#include "platform/mwm_version.hpp"
#include "platform/platform.hpp"

#include "base/logging.hpp"

#include "3party/jansson/myjansson.hpp"

using platform::CountryFile;

namespace storage
{
template <class ToDo>
void LoadGroupImpl(int depth, json_t * group, ToDo & toDo, int64_t version)
{
  if (version::IsSingleMwm(version))
  {
    for (size_t i = 0; i < json_array_size(group); ++i)
    {
      json_t * j = json_array_get(group, i);

      char const * id = json_string_value(json_object_get(j, "id"));
      if (!id)
        MYTHROW(my::Json::Exception, ("LoadGroupImpl. Id is missing.", id));

      uint32_t const mwmSize = static_cast<uint32_t>(json_integer_value(json_object_get(j, "s")));
      // @TODO(bykoianko) After we stop supporting two component mwms (with routing files)
      // rewrite toDo function to use id and mwmSize only once.
      // We expect that mwm and routing files should be less than 2GB.
      toDo(id, id,  mwmSize, 0 /* routingSize */, depth);


      json_t * oldIds = json_object_get(j, "old");
      if (oldIds)
      {
        for (size_t k = 0; k < json_array_size(oldIds); ++k)
        {
          string oldIdValue = json_string_value(json_array_get(oldIds, k));
          toDo(oldIdValue, id);
        }
      }

      json_t * children = json_object_get(j, "g");
      if (children)
        LoadGroupImpl(depth + 1, children, toDo, version);
    }
    return;
  }

  // @TODO(bykoianko) After we stop supporting two component mwms (with routing files)
  // remove code below.
  for (size_t i = 0; i < json_array_size(group); ++i)
  {
    json_t * j = json_array_get(group, i);

    // name is mandatory
    char const * name = json_string_value(json_object_get(j, "n"));
    if (!name)
      MYTHROW(my::Json::Exception, ("Country name is missing"));

    char const * file = json_string_value(json_object_get(j, "f"));
    // if file is empty, it's the same as the name
    if (!file)
      file = name;

    // We expect that mwm and routing files should be less than 2GB.
    toDo(name, file,
         static_cast<uint32_t>(json_integer_value(json_object_get(j, "s"))),
         static_cast<uint32_t>(json_integer_value(json_object_get(j, "rs"))), depth);

    json_t * children = json_object_get(j, "g");
    if (children)
      LoadGroupImpl(depth + 1, children, toDo, version);
  }
}

template <class ToDo>
bool LoadCountriesImpl(string const & jsonBuffer, ToDo & toDo, int64_t version)
{
  try
  {
    my::Json root(jsonBuffer.c_str());
    json_t * children = json_object_get(root.get(), "g");
    if (!children)
      MYTHROW(my::Json::Exception, ("Root country doesn't have any groups"));
    LoadGroupImpl(0, children, toDo, version);
    return true;
  }
  catch (my::Json::Exception const & e)
  {
    LOG(LERROR, (e.Msg()));
    return false;
  }
}

namespace
{
class DoStoreCountries
{
  TCountriesContainer & m_cont;
  TMapping m_idsMapping;

public:
  DoStoreCountries(TCountriesContainer & cont) : m_cont(cont) {}

  void operator()(string const & name, string const & file, uint32_t mapSize,
                  uint32_t routingSize, int depth)
  {
    Country country(file);
    if (mapSize)
    {
      CountryFile countryFile(file);
      countryFile.SetRemoteSizes(mapSize, routingSize);
      country.SetFile(countryFile);
    }
    m_cont.AddAtDepth(depth, country);
  }

  void operator()(TCountryId const & oldId, TCountryId const & newId)
  {
    m_idsMapping[oldId].insert(newId);
  }

  TMapping GetMapping() const { return m_idsMapping; }
};

class DoStoreFile2Info
{
  TMapping m_idsMapping;
  map<string, CountryInfo> & m_file2info;
  int64_t const m_version;

public:
  DoStoreFile2Info(map<string, CountryInfo> & file2info, int64_t version)
    : m_file2info(file2info), m_version(version) {}

  void operator()(string name, string file, uint32_t mapSize, uint32_t, int)
  {
    if (version::IsSingleMwm(m_version))
    {
      ASSERT_EQUAL(name, file, ());
      CountryInfo info(name);
      m_file2info[name] = move(info);
      return;
    }

    if (mapSize)
    {
      CountryInfo info(file);
      m_file2info[file] = info;
    }
  }

  void operator()(TCountryId const & oldId, TCountryId const & newId)
  {
    m_idsMapping[oldId].insert(newId);
  }

  TMapping GetMapping() const { return m_idsMapping; }
};
}  // namespace

int64_t LoadCountries(string const & jsonBuffer, TCountriesContainer & countries, TMapping * mapping /* = nullptr */)
{
  countries.Clear();

  int64_t version = -1;
  try
  {
    my::Json root(jsonBuffer.c_str());
    json_t * const rootPtr = root.get();
    version = json_integer_value(json_object_get(rootPtr, "v"));

    // Extracting root id.
    char const * const idKey = version::IsSingleMwm(version) ? "id" : "n";
    char const * id = json_string_value(json_object_get(rootPtr, idKey));
    if (!id)
      MYTHROW(my::Json::Exception, ("LoadCountries. Id is missing.", id));
    Country rootCountry(id);
    // @TODO(bykoianko) Add CourtyFile to rootCountry with correct size.
    countries.Value() = rootCountry;

    DoStoreCountries doStore(countries);
    if (!LoadCountriesImpl(jsonBuffer, doStore, version))
      return -1;
    if (mapping)
      *mapping = doStore.GetMapping();
  }
  catch (my::Json::Exception const & e)
  {
    LOG(LERROR, (e.Msg()));
  }
  return version;
}

void LoadCountryFile2CountryInfo(string const & jsonBuffer, map<string, CountryInfo> & id2info,
                                 bool & isSingleMwm)
{
  ASSERT(id2info.empty(), ());

  int64_t version = -1;
  try
  {
    my::Json root(jsonBuffer.c_str());
    version = json_integer_value(json_object_get(root.get(), "v"));
    isSingleMwm = version::IsSingleMwm(version);
    DoStoreFile2Info doStore(id2info, version);
    LoadCountriesImpl(jsonBuffer, doStore, version);
  }
  catch (my::Json::Exception const & e)
  {
    LOG(LERROR, (e.Msg()));
  }
}
}  // namespace storage
