/*
 *  Mod Loader - Android port
 *      Unit tests. They exercise everything that does not need a running game:
 *      paths/wildcards, modloader.ini profiles, mod scanning and plugin dispatch, the VFS, the gta.dat merge, the IMG
 *      archive writer and the streaming patcher (against a fake "game" that mimics LoadCdDirectory/AddImageToList).
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "../src/compat.hpp"

#include "../src/util/strutil.hpp"
#include "../src/util/fs.hpp"
#include "../src/util/log.hpp"
#include "../src/core/config.hpp"
#include "../src/core/loader.hpp"
#include "../src/game/gtasa200.hpp"
#include "../src/game/host.hpp"
#include "../src/plugins/vfs.hpp"
#include "../src/plugins/gtadat.hpp"
#include "../src/plugins/img.hpp"
#include "../src/plugins/stream_patcher.hpp"
#include "../src/plugins/std_data.hpp"
#include "../src/plugins/std_stream.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>

using namespace mla;

static int g_checks = 0, g_failures = 0;

#define CHECK(cond) do { ++g_checks; if(!(cond)) { ++g_failures; std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while(0)
#define CHECK_EQ_STR(a, b) do { ++g_checks; std::string _a = (a), _b = (b); if(_a != _b) { ++g_failures; \
    std::fprintf(stderr, "  FAIL %s:%d: %s == %s\n     got      \"%s\"\n     expected \"%s\"\n", __FILE__, __LINE__, #a, #b, _a.c_str(), _b.c_str()); } } while(0)
#define CHECK_EQ_INT(a, b) do { ++g_checks; long long _a = (long long)(a), _b = (long long)(b); if(_a != _b) { ++g_failures; \
    std::fprintf(stderr, "  FAIL %s:%d: %s == %s (got %lld, expected %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while(0)

static void Section(const char* name) { std::printf("[ %s ]\n", name); }

/* ------------------------------------------------------------------------------------------------------------- */
static std::string g_tmp;

static std::string MakeTempRoot()
{
    char tmpl[] = "/tmp/mla_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    return d? d : "/tmp";
}

static void WriteFile(const std::string& path, const std::string& content)
{
    size_t slash = path.find_last_of('/');
    if(slash != std::string::npos) fs::MakeDirs(path.substr(0, slash));
    fs::WriteFileAtomic(path, content);
}

static std::string ReadWhole(const std::string& path)
{
    std::string s;
    fs::ReadFile(path, s);
    return s;
}

/* ------------------------------------------------------------------------------------------------------------- */
static void TestStrings()
{
    Section("strings / paths");

    CHECK_EQ_STR(NormalizePath("DATA\\GTA.DAT"), "DATA/GTA.DAT");
    CHECK_EQ_STR(NormalizePath("MPACK//MPACK1//TEXT.GXT"), "MPACK/MPACK1/TEXT.GXT");
    CHECK_EQ_STR(NormalizePath("./data/gta.dat"), "data/gta.dat");
    CHECK_EQ_STR(NormalizePath(".\\.\\data\\x.dat  "), "data/x.dat");
    CHECK_EQ_STR(NormalizeKey("MODELS\\Gta3.IMG"), "models/gta3.img");
    CHECK_EQ_STR(BaseName("a/b/c.txt"), "c.txt");
    CHECK_EQ_STR(Extension("a/b/C.DFF"), "dff");
    CHECK_EQ_STR(Extension("a/b/noext"), "");

    CHECK(WildcardMatch("*.dff", "Infernus.DFF"));
    CHECK(WildcardMatch("cheetah.dff", "CHEETAH.dff"));
    CHECK(WildcardMatch("path/*.txt", "path/readme.txt"));
    CHECK(WildcardMatch("a?c", "abc"));
    CHECK(!WildcardMatch("a?c", "ac"));
    CHECK(!WildcardMatch("*.dff", "infernus.txd"));
    CHECK(WildcardMatch("*", ""));
    CHECK(WildcardMatch("_ignore", "_IGNORE"));
    CHECK(!WildcardMatch("_ignore", "_ignored"));
}

/* ------------------------------------------------------------------------------------------------------------- */
static void TestConfig()
{
    Section("modloader.ini / config.ini");

    std::string dir = g_tmp + "/cfg";
    WriteFile(dir + "/modloader.ini",
        "[Folder.Config]\nProfile = Gaming ; comment\nPriorityLimit = 90\n"
        "[Profiles.Default.IgnoreMods]\n_ignore\n"
        "[Profiles.Default.Priority]\nSharedMod = 20\n"
        "[Profiles.Gaming.Config]\nParents = Default\nExcludeAllMods = true\n"
        "[Profiles.Gaming.Priority]\nMyMod = 500\nOther Mod = 70\n"
        "[Profiles.Gaming.IncludeMods]\nMy*\n"
        "[Profiles.Gaming.IgnoreFiles]\n*.bak\nbad/*.dff\n"
        "[Profiles.Work.ExclusiveMods]\nWorkOnly\n");

    FolderConfig cfg;
    CHECK(LoadFolderConfig(dir + "/modloader.ini", cfg));
    CHECK_EQ_STR(cfg.profile, "Gaming");
    CHECK_EQ_INT(cfg.priorityLimit, 90);

    CHECK_EQ_INT(cfg.GetModPriority("MyMod"), 90);              // clamped to PriorityLimit
    CHECK_EQ_INT(cfg.GetModPriority("other mod"), 70);          // case insensitive, spaces allowed
    CHECK_EQ_INT(cfg.GetModPriority("SharedMod"), 20);          // inherited from the parent profile
    CHECK_EQ_INT(cfg.GetModPriority("Unlisted"), 50);           // default

    CHECK(!cfg.IsModIgnored("MyMod"));                          // ExcludeAllMods but included by "My*"
    CHECK(cfg.IsModIgnored("Another"));
    CHECK(cfg.IsModIgnored("_ignore"));                         // inherited IgnoreMods
    CHECK(cfg.IsModIgnored("WorkOnly"));                        // exclusive to another profile (and excluded anyway)
    CHECK(cfg.IsFileIgnored("backup.bak", "x/backup.bak"));
    CHECK(cfg.IsFileIgnored("a.dff", "bad/a.dff"));
    CHECK(!cfg.IsFileIgnored("a.dff", "good/a.dff"));

    // defaults written by the loader parse back to the defaults
    WriteFile(dir + "/default.ini", DefaultFolderConfigText());
    FolderConfig def;
    CHECK(LoadFolderConfig(dir + "/default.ini", def));
    CHECK_EQ_STR(def.profile, "Default");
    CHECK(def.IsModIgnored("_ignore"));
    CHECK(!def.IsModIgnored("Anything"));
    CHECK_EQ_INT(def.GetModPriority("Anything"), 50);

    WriteFile(dir + "/config.ini", "[Config]\nEnableLog = false\nMaxLogSize = 1024\n[Android]\nEnableStream = no\n");
    BaseConfig base;
    CHECK(LoadBaseConfig(dir + "/config.ini", base));
    CHECK(!base.enableLog);
    CHECK(base.enablePlugins);
    CHECK_EQ_INT(base.maxLogSize, 1024);
    CHECK(!base.enableStream);
    CHECK(base.enableData);

    WriteFile(dir + "/defcfg.ini", DefaultBaseConfigText());
    BaseConfig base2;
    CHECK(LoadBaseConfig(dir + "/defcfg.ini", base2));
    CHECK(base2.enableLog && base2.enableData && base2.enableStream && base2.injectIdeIpl);
}

/* ------------------------------------------------------------------------------------------------------------- */
static void TestGtaDat()
{
    Section("gta.dat merge");

    const std::string dat =
        "# comment\r\n"
        "IMG MODELS\\GTA_INT.IMG\r\n"
        "IDE DATA\\MAPS\\generic\\vegepart.IDE\r\n"
        "IDE DATA\\VEHICLES.IDE\r\n"
        "\r\n"
        "IPL DATA\\MAPS\\LA\\LAe.ipl\r\n"
        "IPL DATA\\MAPS\\LA\\LAn.ipl\r\n"
        "EXIT\r\n";

    std::set<std::string> names = GtaDatReferencedNames(dat);
    CHECK(names.count("vegepart.ide") == 1);
    CHECK(names.count("vehicles.ide") == 1);
    CHECK(names.count("lae.ipl") == 1);
    CHECK(names.count("gta_int.img") == 0);                     // only IDE/IPL

    std::vector<GtaDatItem> items = { {"IPL", "modloader/.vfs/m002.ipl"}, {"IDE", "modloader/.vfs/m001.ide"} };
    std::string merged = MergeGtaDat(dat, items);

    size_t ide  = merged.find("IDE modloader/.vfs/m001.ide\r\n");
    size_t ipl0 = merged.find("IPL DATA\\MAPS\\LA\\LAe.ipl");
    size_t ipl  = merged.find("IPL modloader/.vfs/m002.ipl\r\n");
    size_t exit = merged.find("EXIT\r\n");
    CHECK(ide != std::string::npos && ipl0 != std::string::npos && ipl != std::string::npos && exit != std::string::npos);
    CHECK(ide < ipl0);                                          // IDE goes before the first IPL
    CHECK(ipl > ipl0 && ipl < exit);                            // new IPL goes at the end, before EXIT
    CHECK(merged.find("\r\n") != std::string::npos);            // keeps CRLF
    CHECK(merged.find("\n") == merged.find("\r\n") + 1);        // no bare LF introduced

    // LF file without IPL nor EXIT
    std::string merged2 = MergeGtaDat("IDE a.ide\nIDE b.ide\n", items);
    CHECK_EQ_STR(merged2, "IDE a.ide\nIDE b.ide\nIDE modloader/.vfs/m001.ide\nIPL modloader/.vfs/m002.ipl\n");

    CHECK_EQ_STR(MergeGtaDat("x\n", {}), "x\n");
}

/* ------------------------------------------------------------------------------------------------------------- */
static void TestVfs()
{
    Section("vfs priorities");

    Vfs vfs;
    VfsFile a; a.abs = "/m/A/data/handling.cfg"; a.rel = "data/handling.cfg"; a.priority = 50; a.modId = 1;
    VfsFile b; b.abs = "/m/B/cars/handling.cfg"; b.rel = "cars/handling.cfg"; b.priority = 50; b.modId = 2;
    VfsFile c; c.abs = "/m/C/data/handling.cfg"; c.rel = "data/handling.cfg"; c.priority = 10; c.modId = 3;
    vfs.Add(a, true); vfs.Add(b, true); vfs.Add(c, true);

    // exact path: A (50) beats C (10); by name only: B comes later than A with the same priority
    CHECK_EQ_STR(vfs.Find("data/handling.cfg")->abs, "/m/A/data/handling.cfg");
    CHECK_EQ_STR(vfs.Find("cars/handling.cfg")->abs, "/m/B/cars/handling.cfg");
    CHECK_EQ_STR(vfs.Find("anywhere/handling.cfg")->abs, "/m/B/cars/handling.cfg");     // by name: B wins (same priority, later)
    CHECK(vfs.Find("data/other.cfg") == nullptr);

    VfsFile s; s.abs = "/m/S/vehicle.txd"; s.rel = "vehicle.txd"; s.priority = 50; s.modId = 4;
    vfs.Add(s, /*allowName=*/false);
    CHECK(vfs.Find("vehicle.txd") != nullptr);                  // exact only
    CHECK(vfs.Find("models/generic/vehicle.txd") == nullptr);   // not reachable by name

    vfs.AddAlias("modloader/.vfs/m001.ide", "/m/X/new.ide");
    CHECK_EQ_STR(vfs.Find("modloader/.vfs/m001.ide")->abs, "/m/X/new.ide");
}

/* ------------------------------------------------------------------------------------------------------------- */
static void TestIm()
{
    Section("IMG archive");

    std::string dir = g_tmp + "/img";
    std::string payloadA(5000, 'a'), payloadB(2048, 'b'), payloadC(10, 'c');
    WriteFile(dir + "/a.dff", payloadA);
    WriteFile(dir + "/b.txd", payloadB);
    WriteFile(dir + "/c.col", payloadC);
    WriteFile(dir + "/empty.dff", "");

    std::vector<ImgSource> src;
    for(auto n : {"b.txd", "A.DFF", "c.col", "empty.dff", "this_name_is_way_too_long_for_img.dff"})
    {
        ImgSource s;
        s.name = n;
        s.abs  = dir + "/" + ((std::string(n) == "A.DFF")? "a.dff" : n);
        fs::Stat st = fs::GetStat(s.abs);
        s.size = st.size; s.mtime = st.mtime;
        src.push_back(s);
    }

    std::vector<std::string> skipped;
    ImgLayout lay = LayoutImg(src, &skipped);
    CHECK_EQ_INT(lay.entries.size(), 3);
    CHECK_EQ_INT(skipped.size(), 2);                            // empty file + long name
    CHECK_EQ_STR(lay.entries[0].name, "a.dff");                 // sorted, lower case
    CHECK_EQ_INT(lay.entries[0].offset, 1);                     // directory takes 1 sector
    CHECK_EQ_INT(lay.entries[0].sectors, 3);                    // 5000 bytes
    CHECK_EQ_INT(lay.entries[1].offset, 4);
    CHECK_EQ_INT(lay.entries[1].sectors, 1);                    // exactly 2048 bytes
    CHECK_EQ_INT(lay.entries[2].offset, 5);
    CHECK_EQ_INT(lay.totalSize, 6 * 2048);
    CHECK_EQ_INT(lay.maxSectors, 3);
    CHECK(lay.Find("b.txd") != nullptr && lay.Find("nope.dff") == nullptr);

    std::string img = dir + "/stream.img", sig = dir + "/stream.sig";
    CHECK(!IsImgUpToDate(img, sig, lay));
    CHECK(WriteImg(img, lay));
    CHECK(WriteImgSignature(sig, lay));
    CHECK(IsImgUpToDate(img, sig, lay));

    std::string bytes = ReadWhole(img);
    CHECK_EQ_INT(bytes.size(), lay.totalSize);
    CHECK(bytes.compare(0, 4, "VER2") == 0);

    std::vector<ImgDirName> names = ParseImgDirectory((const uint8_t*)bytes.data(), bytes.size());
    CHECK_EQ_INT(names.size(), 3);
    CHECK_EQ_STR(names[0].lowerName, "a.dff");
    CHECK_EQ_INT(names[2].offset, 5);
    CHECK(bytes.compare(1 * 2048, 5000, payloadA) == 0);
    CHECK(bytes.compare(4 * 2048, 2048, payloadB) == 0);
    CHECK(bytes.compare(5 * 2048, 10, payloadC) == 0);
    CHECK(bytes[5 * 2048 + 10] == 0);                           // zero padding

    // the 16 bit size field of the entry holds the sector count
    CHECK_EQ_INT((unsigned char)bytes[8 + 4] | ((unsigned char)bytes[8 + 5] << 8), 3);

    // changing a file changes the signature
    WriteFile(dir + "/c.col", std::string(11, 'z'));
    ImgSource& sc = src[2];
    fs::Stat st = fs::GetStat(sc.abs); sc.size = st.size; sc.mtime = st.mtime + 5;
    ImgLayout lay2 = LayoutImg(src, nullptr);
    CHECK(lay2.signature != lay.signature);
    CHECK(!IsImgUpToDate(img, sig, lay2));
}

/* ------------------------------------------------------------------------------------------------------------- */
/*  A fake game that does what CStreaming::LoadCdDirectory / AddImageToList do (first entry wins, etc.)             */
struct FakeGame
{
    gtasa::StreamingFile                files[8];
    std::vector<gtasa::CStreamingInfo>  infos;
    int                                 bufferSize = 0;
    std::map<std::string, std::vector<uint8_t>> directories;    // path -> "VER2" + count + entries
    std::map<std::string, unsigned>     modelIndex;             // lower case entry name -> streaming info index
    int                                 nextHandle = 1;
    bool                                failOpen = false;
    std::vector<std::string>            loadCalls;

    FakeGame() : infos(gtasa::kNumStreamingInfos)
    {
        std::memset(files, 0, sizeof(files));
        std::memset(infos.data(), 0, infos.size() * sizeof(infos[0]));
    }

    static std::vector<uint8_t> BuildDirectory(const std::vector<std::tuple<std::string, uint32_t, uint32_t>>& entries)
    {
        std::vector<uint8_t> d(8 + entries.size() * 32, 0);
        std::memcpy(d.data(), "VER2", 4);
        uint32_t n = (uint32_t)entries.size();
        std::memcpy(d.data() + 4, &n, 4);
        size_t pos = 8;
        for(auto& e : entries)
        {
            uint32_t off = std::get<1>(e); uint16_t sz = (uint16_t)std::get<2>(e);
            std::memcpy(&d[pos], &off, 4);
            std::memcpy(&d[pos + 4], &sz, 2);
            std::memcpy(&d[pos + 8], std::get<0>(e).c_str(), std::get<0>(e).size());
            pos += 32;
        }
        return d;
    }

    void SetImage(int slot, const char* name, bool flag)
    {
        std::snprintf(files[slot].name, sizeof(files[slot].name), "%s", name);
        files[slot].notPlayerImg = flag? 1 : 0;
        files[slot].streamHandle = (int32_t)((unsigned)slot << 24);
    }

    /* CStreaming::LoadCdDirectory(path, idx) */
    void OriginalLoad(const char* path, int idx)
    {
        loadCalls.push_back(path);
        auto it = directories.find(path);
        if(it == directories.end()) return;
        auto names = ParseImgDirectory(it->second.data(), it->second.size());
        for(size_t i = 0; i < names.size(); ++i)
        {
            uint16_t size; std::memcpy(&size, &it->second[8 + i * 32 + 4], 2);
            auto m = modelIndex.find(names[i].lowerName);
            if(m == modelIndex.end()) continue;                 // not a model the game knows about
            gtasa::CStreamingInfo& info = infos[m->second];
            if(info.cdSize != 0) continue;                      // the first entry found wins
            info.imgId = (uint8_t)idx; info.cdPosn = names[i].offset; info.cdSize = size;
            if(bufferSize < (int)size) bufferSize = size;
        }
    }

    StreamPatcher::Api MakeApi()
    {
        StreamPatcher::Api api;
        api.files = files; api.infos = infos.data(); api.bufferSize = &bufferSize;
        api.addImageToList = [this](const char* path, bool flag) -> int {
            for(int i = 0; i < 8; ++i)
                if(files[i].name[0] == '\0')
                {
                    std::snprintf(files[i].name, sizeof(files[i].name), "%s", path);
                    files[i].notPlayerImg = flag? 1 : 0;
                    files[i].streamHandle = failOpen? 0 : (int32_t)((unsigned)(nextHandle++ + 1) << 24);
                    return i;
                }
            return 0;
        };
        api.readDirectory = [this](const char* path, std::vector<uint8_t>& out) -> bool {
            auto it = directories.find(path);
            if(it == directories.end()) return false;
            out = it->second; return true;
        };
        api.originalLoadCdDirectory = [this](const char* path, int idx) { OriginalLoad(path, idx); };
        return api;
    }

    /* The loop of CStreaming::Init2 */
    void Init2Loop(StreamPatcher& patcher)
    {
        for(int i = 0; files[i].name[0] != '\0'; ++i)
            if(files[i].notPlayerImg)
            {
                if(patcher.IsEnabled()) patcher.OnLoadCdDirectory(files[i].name, i);
                else                    OriginalLoad(files[i].name, i);
            }
    }
};

static ImgLayout MakeLayout(const std::string& dir, std::vector<std::pair<std::string, size_t>> files)
{
    std::vector<ImgSource> src;
    for(auto& f : files)
    {
        WriteFile(dir + "/" + f.first, std::string(f.second, 'x'));
        ImgSource s; s.name = f.first; s.abs = dir + "/" + f.first; s.size = f.second; s.mtime = 1;
        src.push_back(s);
    }
    return LayoutImg(src, nullptr);
}

static void SetupOriginalImages(FakeGame& g)
{
    g.SetImage(0, "MODELS\\GTA3.IMG", true);
    g.SetImage(1, "MODELS\\GTA_INT.IMG", true);
    g.directories["MODELS\\GTA3.IMG"] = FakeGame::BuildDirectory({
        std::make_tuple("infernus.dff", 100u, 10u), std::make_tuple("vehicle.txd", 200u, 20u), std::make_tuple("other.dff", 300u, 5u) });
    g.directories["MODELS\\GTA_INT.IMG"] = FakeGame::BuildDirectory({ std::make_tuple("int_room.dff", 50u, 4u) });
    g.modelIndex = { {"infernus.dff", 400}, {"vehicle.txd", 20010}, {"other.dff", 401}, {"int_room.dff", 402}, {"newmodel.dff", 403} };
}

static void TestStreamPatcherEarly()
{
    Section("std.stream patcher (early mode)");

    FakeGame g;
    SetupOriginalImages(g);

    ImgLayout lay = MakeLayout(g_tmp + "/sp1", { {"infernus.dff", 70000}, {"vehicle.txd", 100}, {"int_room.dff", 5000}, {"newmodel.dff", 3000}, {"unknown.col", 10} });
    // sectors: infernus 35, vehicle 1, int_room 3, newmodel 2, unknown 1 -> infernus is bigger than any original entry (10)

    // what the game finds when it reads the generated archive: its directory
    {
        std::vector<std::tuple<std::string, uint32_t, uint32_t>> entries;
        for(auto& e : lay.entries) entries.push_back(std::make_tuple(e.name, e.offset, e.sectors));
        g.directories["modloader/.vfs/stream.img"] = FakeGame::BuildDirectory(entries);
    }

    StreamPatcher patcher;
    patcher.Configure(g.MakeApi(), &lay, "modloader/.vfs/stream.img");
    CHECK(patcher.IsEnabled());

    g.Init2Loop(patcher);

    // the generated archive took the first free slot and the game loaded its directory itself
    CHECK_EQ_INT(patcher.ImageIndex(), 2);
    CHECK_EQ_STR(g.files[2].name, "modloader/.vfs/stream.img");
    CHECK(g.files[2].streamHandle != 0);
    CHECK_EQ_INT(g.loadCalls.size(), 3);

    // replaced entries now point to the generated archive
    const ImgEntry* inf = lay.Find("infernus.dff");
    CHECK_EQ_INT(g.infos[400].imgId, 2);
    CHECK_EQ_INT(g.infos[400].cdPosn, inf->offset);
    CHECK_EQ_INT(g.infos[400].cdSize, 35);
    CHECK_EQ_INT(g.infos[20010].imgId, 2);
    CHECK_EQ_INT(g.infos[20010].cdPosn, lay.Find("vehicle.txd")->offset);
    CHECK_EQ_INT(g.infos[402].imgId, 2);                        // in an image other than the first one
    CHECK_EQ_INT(g.infos[402].cdSize, 3);

    // untouched entry keeps pointing to the original image
    CHECK_EQ_INT(g.infos[401].imgId, 0);
    CHECK_EQ_INT(g.infos[401].cdPosn, 300);
    CHECK_EQ_INT(g.infos[401].cdSize, 5);

    // a new model was registered by the game itself, straight from our archive
    CHECK_EQ_INT(g.infos[403].imgId, 2);
    CHECK_EQ_INT(g.infos[403].cdPosn, lay.Find("newmodel.dff")->offset);

    // the streaming buffer grew to fit the biggest replacement
    CHECK_EQ_INT(g.bufferSize, 35);
    CHECK_EQ_INT(patcher.PatchedCount(), 3);
}

static void TestStreamPatcherLate()
{
    Section("std.stream patcher (late mode)");

    FakeGame g;
    SetupOriginalImages(g);
    // the game already loaded everything and allocated its buffers
    g.OriginalLoad("MODELS\\GTA3.IMG", 0);
    g.OriginalLoad("MODELS\\GTA_INT.IMG", 1);
    g.bufferSize = 10;
    for(unsigned i = 1000; i < 1200; ++i) { g.infos[i].cdSize = 1; g.infos[i].imgId = 0; g.infos[i].cdPosn = 900000 + i; }   // plenty of entries

    ImgLayout lay = MakeLayout(g_tmp + "/sp2", { {"infernus.dff", 70000}, {"vehicle.txd", 100} });

    StreamPatcher patcher;
    patcher.Configure(g.MakeApi(), &lay, "modloader/.vfs/stream.img");
    CHECK(patcher.StreamingAlreadyInitialised());
    patcher.PatchLate();

    CHECK_EQ_INT(patcher.ImageIndex(), 2);
    CHECK_EQ_INT(g.infos[20010].imgId, 2);                      // fits the buffer: replaced
    CHECK_EQ_INT(g.infos[400].imgId, 0);                        // 35 sectors > 10: skipped, original kept
    CHECK_EQ_INT(patcher.SkippedCount(), 1);
    CHECK_EQ_INT(g.bufferSize, 10);                             // never touched in late mode
    CHECK_EQ_INT(g.files[2].notPlayerImg, 0);                   // the game must not parse it again

    // a game that is not initialised yet is not mistaken for an initialised one
    FakeGame g2;
    g2.SetImage(0, "MODELS\\GTA3.IMG", true);
    StreamPatcher p2;
    p2.Configure(g2.MakeApi(), &lay, "x");
    CHECK(!p2.StreamingAlreadyInitialised());
}

static void TestStreamPatcherFailures()
{
    Section("std.stream patcher (failures)");

    ImgLayout lay = MakeLayout(g_tmp + "/sp3", { {"infernus.dff", 100} });

    // the game cannot open our archive (CdStreamOpen returns handle 0 = "gta3.img"): must never be used
    {
        FakeGame g; SetupOriginalImages(g); g.failOpen = true;
        StreamPatcher p; p.Configure(g.MakeApi(), &lay, "modloader/.vfs/stream.img");
        g.Init2Loop(p);
        CHECK(!p.IsEnabled());
        CHECK_EQ_STR(g.files[2].name, "");                       // slot given back
        CHECK_EQ_INT(g.infos[400].imgId, 0);
        CHECK_EQ_INT(g.infos[400].cdPosn, 100);
        CHECK_EQ_INT(g.loadCalls.size(), 2);                     // both original images were still loaded
    }

    // all slots in use
    {
        FakeGame g; SetupOriginalImages(g);
        for(int i = 2; i < 8; ++i) g.SetImage(i, "FILLER.IMG", false);
        StreamPatcher p; p.Configure(g.MakeApi(), &lay, "modloader/.vfs/stream.img");
        g.Init2Loop(p);
        CHECK(!p.IsEnabled());
        CHECK_EQ_INT(g.infos[400].imgId, 0);
    }

    // first free slot is beyond what ms_imageOffsets can hold
    {
        FakeGame g; SetupOriginalImages(g);
        for(int i = 2; i < 6; ++i) g.SetImage(i, "FILLER.IMG", false);
        StreamPatcher p; p.Configure(g.MakeApi(), &lay, "modloader/.vfs/stream.img");
        g.Init2Loop(p);
        CHECK(!p.IsEnabled());
    }
}

/* ------------------------------------------------------------------------------------------------------------- */
static void TestLoaderAndPlugins()
{
    Section("loader + std.data + std.stream");

    std::string root = g_tmp + "/Android/data/com.test.gtasa/files";
    std::string ml = root + "/modloader";

    WriteFile(ml + "/Alpha/data/handling.cfg", "alpha");
    WriteFile(ml + "/Beta/Cars/handling.cfg", "beta");
    WriteFile(ml + "/Beta/Cars/infernus.dff", "dff-from-beta");
    WriteFile(ml + "/Beta/Cars/readme.bak", "ignore me");
    WriteFile(ml + "/Beta/text/american.gxt", "gxt");
    WriteFile(ml + "/Beta/scripts/main.scm", "scm");
    WriteFile(ml + "/Gamma mod/maps/new objects.ide", "objs\n1, x, y, 1, 100, 0\nend\n");
    WriteFile(ml + "/Gamma mod/maps/new_map.ipl", "inst\nend\n");
    WriteFile(ml + "/Gamma mod/maps/vehicles.ide", "replacement of the game file");
    WriteFile(ml + "/Gamma mod/maps/countn2_stream0.ipl", std::string("bnry") + std::string(60, 'b'));
    WriteFile(ml + "/Gamma mod/maps/lae.ipl", "text ipl that replaces the game's");
    WriteFile(ml + "/_ignore/foo.dat", "ignored mod");
    WriteFile(ml + "/Zero/foo.dat", "priority zero");
    WriteFile(ml + "/.hidden/foo.dat", "hidden");
    WriteFile(ml + "/modloader.ini",
        "[Folder.Config]\nProfile=Default\n"
        "[Profiles.Default.IgnoreMods]\n_ignore\n"
        "[Profiles.Default.IgnoreFiles]\n*.bak\n"
        "[Profiles.Default.Priority]\nAlpha=80\nBeta=60\nZero=0\n");

    Loader& L = Loader::Get();
    StdData::Get().Reset();
    StdStream::Get().Reset();
    CHECK(L.Init(root));
    L.RegisterPlugin(StdStream::Get().Plugin());        // registered in the "wrong" order on purpose, priorities sort it out
    L.RegisterPlugin(StdData::Get().Plugin());
    L.Start();

    // ---- mods ----
    CHECK_EQ_INT(L.Mods().size(), 3);                    // _ignore, Zero (priority 0) and .hidden are out
    CHECK_EQ_STR(L.Mods()[0]->name, "Gamma mod");        // 50
    CHECK_EQ_STR(L.Mods()[1]->name, "Beta");             // 60
    CHECK_EQ_STR(L.Mods()[2]->name, "Alpha");            // 80
    CHECK_EQ_STR(L.Mods()[2]->gamePath, "modloader/Alpha/");
    for(auto& f : L.Files())
        CHECK(f->rel.find(".bak") == std::string::npos);

    // ---- the C interface data the plugins see ----
    const ModFile* hf = nullptr;
    for(auto& f : L.Files()) if(f->rel == "data/handling.cfg") hf = f.get();
    CHECK(hf != nullptr);
    if(hf)
    {
        CHECK_EQ_STR(hf->c.buffer, "modloader/Alpha/data/handling.cfg");
        CHECK_EQ_STR(hf->c.buffer + hf->c.pos_filedir, "data/handling.cfg");
        CHECK_EQ_STR(hf->c.buffer + hf->c.pos_filename, "handling.cfg");
        CHECK_EQ_STR(hf->c.buffer + hf->c.pos_filext, "cfg");
        CHECK_EQ_INT(hf->c.pos_eos, strlen(hf->c.buffer));
        CHECK_EQ_INT(hf->c.size, 5);
        CHECK(hf->c.parent && hf->c.parent->priority == 80);
    }

    // ---- std.data: file replacement ----
    StdData& D = StdData::Get();
    CHECK_EQ_STR(D.ResolveAbs("DATA\\HANDLING.CFG"), ml + "/Alpha/data/handling.cfg");          // exact path, highest priority
    CHECK_EQ_STR(D.ResolveAbs("data/Handling.cfg"), ml + "/Alpha/data/handling.cfg");
    CHECK_EQ_STR(D.ResolveAbs("TEXT\\AMERICAN.GXT"), ml + "/Beta/text/american.gxt");
    CHECK_EQ_STR(D.ResolveAbs("SOME\\OTHER\\DIR\\AMERICAN.GXT"), ml + "/Beta/text/american.gxt");   // by name
    CHECK_EQ_STR(D.ResolveAbs("DATA\\SCRIPT\\MAIN.SCM"), ml + "/Beta/scripts/main.scm");     // scripts are reachable by name
    CHECK_EQ_STR(D.ResolveAbs("DATA\\NOTHING.DAT"), "");
    CHECK_EQ_STR(D.ResolveAbs("MODELS\\GTA3.IMG"), "");

    // stream files are NOT reachable by name through std.data, only by their exact path
    CHECK_EQ_STR(D.ResolveAbs("infernus.dff"), "");
    CHECK_EQ_STR(D.ResolveAbs("cars/infernus.dff"), ml + "/Beta/Cars/infernus.dff");

    // name the game has to be told to open
    CHECK_EQ_STR(D.Redirect("DATA\\HANDLING.CFG"), "/Android/data/com.test.gtasa/files/modloader/Alpha/data/handling.cfg");
    CHECK_EQ_STR(StdData::MakeGameName("/sdcard/Android/data/x/files/a b/c.dat"), "/Android/data/x/files/a b/c.dat");
    CHECK_EQ_STR(StdData::MakeGameName("/data/data/x/files/a.dat"), "");

    // ---- std.data: gta.dat ----
    D.SetOriginalReader([](const char* name, std::string& out) -> bool {
        std::string n = NormalizeKey(name);
        if(n == "data/gta.dat")     { out = "IDE DATA\\VEHICLES.IDE\nIPL DATA\\MAPS\\LA\\LAe.ipl\nEXIT\n"; return true; }
        if(n == "data/default.dat") { out = "IDE DATA\\DEFAULT.IDE\n"; return true; }
        return false;
    });

    std::string gta = D.ResolveAbs("DATA\\GTA.DAT");
    CHECK_EQ_STR(gta, ml + "/.data/cache/gta.dat");
    std::string merged = ReadWhole(gta);
    // "new objects.ide" (space in the name) and new_map.ipl are additions, vehicles.ide/lae.ipl are replacements of game files,
    // the binary countn2_stream0.ipl belongs to std.stream
    CHECK(merged.find("IDE modloader/.vfs/m001.ide") != std::string::npos);
    CHECK(merged.find("IPL modloader/.vfs/m002.ipl") != std::string::npos);
    CHECK(merged.find("m003") == std::string::npos);
    CHECK(merged.find("IDE modloader/.vfs/m001.ide") < merged.find("IPL DATA"));
    CHECK(merged.find("IPL modloader/.vfs/m002.ipl") > merged.find("IPL DATA"));
    CHECK(merged.find("IPL modloader/.vfs/m002.ipl") < merged.find("EXIT"));
    CHECK_EQ_STR(D.ResolveAbs("modloader/.vfs/m001.ide"), ml + "/Gamma mod/maps/new objects.ide");
    CHECK_EQ_STR(D.ResolveAbs("MODLOADER\\.VFS\\M002.IPL"), ml + "/Gamma mod/maps/new_map.ipl");
    CHECK_EQ_STR(D.ResolveAbs("DATA\\VEHICLES.IDE"), ml + "/Gamma mod/maps/vehicles.ide");            // replacement by name
    CHECK_EQ_STR(D.ResolveAbs("DATA\\GTA.DAT"), gta);                                                   // cached, same file

    // ---- std.stream ----
    StdStream& S = StdStream::Get();
    CHECK(S.Sources().count("infernus.dff") == 1);
    CHECK(S.Sources().count("main.scm") == 1);
    CHECK(S.Sources().count("countn2_stream0.ipl") == 1);       // binary IPL
    CHECK(S.Sources().count("new_map.ipl") == 0);               // text IPL
    CHECK(S.Sources().count("handling.cfg") == 0);
    CHECK_EQ_INT(S.Layout().entries.size(), 3);
    CHECK(fs::Exists(ml + "/.data/cache/stream.img"));
    CHECK(fs::Exists(ml + "/.data/cache/stream.sig"));
    CHECK_EQ_STR(D.ResolveAbs(StdStream::Alias()), ml + "/.data/cache/stream.img");
    CHECK_EQ_INT(fs::GetStat(ml + "/.data/cache/stream.img").size, S.Layout().totalSize);

    // log, config files were created
    CHECK(fs::Exists(ml + "/.data/config.ini"));
    CHECK(fs::Exists(ml + "/modloader.log"));
    L.Shutdown();
}


/* ------------------------------------------------------------------------------------------------------------- */
/*  The glue between the plugins and the game, exercised through a fake host: the "game functions" below are        */
/*  ordinary C++ functions that behave like the real ones (OS_FileRead returns 0 on success and 2 at the end of    */
/*  a file, CdStreamOpen-style handles, first free slot of AddImageToList...).                                      */
/* ------------------------------------------------------------------------------------------------------------- */
namespace glue
{
    struct FakeFile { std::string data; size_t pos = 0; };

    static std::map<uintptr_t, FakeFile>        files;            // open handles
    static std::map<std::string, std::string>   gameFiles;        // what the "game" has: name as asked (lower case, '/') -> contents
    static std::map<std::string, std::string>   absFiles;         // what exists for absolute names ("/Android/...") -> contents
    static std::vector<std::string>             openCalls;        // every name the original OS_FileOpen received
    static uintptr_t                            nextHandle = 0x1000;
    static FakeGame                             game;
    static std::map<void*, void*>               hooks;            // original function -> its replacement

    static int OS_FileOpen(int area, void** out, const char* name, int access)
    {
        openCalls.push_back(name);
        std::string n = name;
        const std::string* data = nullptr;
        if(!n.empty() && n[0] == '/') { auto it = absFiles.find(n); if(it != absFiles.end()) data = &it->second; }
        else                          { auto it = gameFiles.find(NormalizeKey(n)); if(it != gameFiles.end()) data = &it->second; }
        if(!data) return 1;
        uintptr_t h = nextHandle++;
        files[h].data = *data;
        *out = (void*)h;
        return 0;
    }

    static int OS_FileRead(void* handle, void* buffer, int size)
    {
        auto it = files.find((uintptr_t)handle);
        if(it == files.end()) return 3;
        FakeFile& f = it->second;
        if(f.pos + (size_t)size <= f.data.size()) { std::memcpy(buffer, f.data.data() + f.pos, (size_t)size); f.pos += (size_t)size; return 0; }
        f.pos = f.data.size();
        return 2;                                                   // end of file
    }

    static void OS_FileClose(void* handle) { files.erase((uintptr_t)handle); }

    static unsigned CFileMgr_OpenFile(const char* path, const char*)
    {
        auto it = game.directories.find(path);
        if(it == game.directories.end()) return 0;
        uintptr_t h = nextHandle++;
        files[h].data.assign((const char*)it->second.data(), it->second.size());
        return (unsigned)h;
    }

    static void CFileMgr_CloseFile(unsigned h) { files.erase((uintptr_t)h); }
    static int  AddImageToList(const char* path, bool flag) { return game.MakeApi().addImageToList(path, flag); }
    static void LoadCdDirectory(const char* path, int idx)  { game.OriginalLoad(path, idx); }

    static uintptr_t Sym(const char* name)
    {
        std::string n = name;
        if(n == gtasa::sym::OS_FileOpen)            return (uintptr_t)&OS_FileOpen;
        if(n == gtasa::sym::OS_FileRead)            return (uintptr_t)&OS_FileRead;
        if(n == gtasa::sym::OS_FileClose)           return (uintptr_t)&OS_FileClose;
        if(n == gtasa::sym::CFileMgr_OpenFile)      return (uintptr_t)&CFileMgr_OpenFile;
        if(n == gtasa::sym::CFileMgr_CloseFile)     return (uintptr_t)&CFileMgr_CloseFile;
        if(n == gtasa::sym::AddImageToList)         return (uintptr_t)&AddImageToList;
        if(n == gtasa::sym::LoadCdDirectory)        return (uintptr_t)&LoadCdDirectory;
        if(n == gtasa::sym::ms_files)               return (uintptr_t)game.files;
        if(n == gtasa::sym::ms_aInfoForModel)       return (uintptr_t)game.infos.data();
        if(n == gtasa::sym::ms_streamingBufferSize) return (uintptr_t)&game.bufferSize;
        return 0;
    }

    static bool Hook(void* target, void* replacement, void** original)
    {
        if(!target || !replacement || !original) return false;
        hooks[target] = replacement;
        *original = target;                                         // "trampoline" = the original function
        return true;
    }

    /* A call the game makes, routed through the hook like the patched function would be */
    static int Open(int area, const char* name, int access, void** out)
    {
        auto fn = (int(*)(int, void**, const char*, int))hooks[(void*)&OS_FileOpen];
        return fn(area, out, name, access);
    }
}

static void TestHostGlue()
{
    Section("hooks through a fake host");

    std::string root = g_tmp + "/Android/data/com.test.glue/files";
    std::string ml = root + "/modloader";

    WriteFile(ml + "/Alpha/data/handling.cfg", "alpha");
    WriteFile(ml + "/Alpha/data/broken.cfg", "will fail to open");
    WriteFile(ml + "/Cars/infernus.dff", std::string(5000, 'i'));
    WriteFile(ml + "/Cars/vehicle.txd", std::string(100, 't'));
    WriteFile(ml + "/Maps/objects.ide", "objs\nend\n");

    // what the (fake) game has
    glue::gameFiles["data/handling.cfg"] = "original handling";
    glue::gameFiles["data/broken.cfg"]   = "original broken";
    glue::gameFiles["data/gta.dat"]      = "IMG MODELS\\GTA_INT.IMG\nIDE DATA\\VEHICLES.IDE\nIPL DATA\\MAPS\\LA\\LAe.ipl\n";
    glue::gameFiles["data/other.dat"]    = "other";
    glue::absFiles["/Android/data/com.test.glue/files/modloader/Alpha/data/handling.cfg"] = "alpha";
    glue::absFiles["/Android/data/com.test.glue/files/modloader/.data/cache/gta.dat"]     = "(replaced below)";
    // "/Android/.../broken.cfg" is deliberately missing: opening the replacement fails and the game must get the original

    SetupOriginalImages(glue::game);
    glue::game.nextHandle = 2;

    static const Host host = { &glue::Hook, &glue::Sym };
    gtasa::SetFingerprintChecks(false);

    Loader& L = Loader::Get();
    L.ResetForTests();
    StdData::Get().Reset();
    StdStream::Get().Reset();
    SetHost(&host);

    CHECK(L.Init(root));
    L.RegisterPlugin(StdData::Get().Plugin());
    L.RegisterPlugin(StdStream::Get().Plugin());
    L.Start();

    CHECK(StdData::Get().IsHooked());
    CHECK(glue::hooks.count((void*)&glue::OS_FileOpen) == 1);
    CHECK(glue::hooks.count((void*)&glue::LoadCdDirectory) == 1);

    // ---- OS_FileOpen ----
    void* h = nullptr;
    glue::openCalls.clear();
    CHECK_EQ_INT(glue::Open(0, "DATA\\HANDLING.CFG", gtasa::kAccessRead, &h), 0);
    CHECK(h != nullptr);
    CHECK_EQ_INT(glue::openCalls.size(), 1);
    CHECK_EQ_STR(glue::openCalls[0], "/Android/data/com.test.glue/files/modloader/Alpha/data/handling.cfg");     // redirected to the mod
    char buf[16] = {0};
    CHECK_EQ_INT(glue::OS_FileRead(h, buf, 5), 0);
    CHECK_EQ_STR(buf, "alpha");
    glue::OS_FileClose(h);

    glue::openCalls.clear(); h = nullptr;
    CHECK_EQ_INT(glue::Open(0, "DATA\\OTHER.DAT", gtasa::kAccessRead, &h), 0);                  // not provided by any mod
    CHECK_EQ_STR(glue::openCalls[0], "DATA\\OTHER.DAT");

    glue::openCalls.clear(); h = nullptr;
    CHECK_EQ_INT(glue::Open(1, "DATA\\HANDLING.CFG", gtasa::kAccessRead, &h), 0);               // user area (saves): never redirected, the original opens
    CHECK_EQ_STR(glue::openCalls[0], "DATA\\HANDLING.CFG");

    glue::openCalls.clear(); h = nullptr;
    glue::Open(0, "DATA\\HANDLING.CFG", gtasa::kAccessWrite, &h);                                // writes: never redirected
    CHECK_EQ_STR(glue::openCalls[0], "DATA\\HANDLING.CFG");

    glue::openCalls.clear(); h = nullptr;
    CHECK_EQ_INT(glue::Open(0, "DATA\\BROKEN.CFG", gtasa::kAccessRead, &h), 0);                  // replacement cannot be opened -> original
    CHECK_EQ_INT(glue::openCalls.size(), 2);
    CHECK_EQ_STR(glue::openCalls[1], "DATA\\BROKEN.CFG");

    // ---- gta.dat: the original is read through OS_FileRead (0 = ok, 2 = end of file), merged, and served from the cache ----
    glue::openCalls.clear(); h = nullptr;
    glue::absFiles["/Android/data/com.test.glue/files/modloader/.data/cache/gta.dat"] = "";     // the fake FS needs to know the file exists
    int rc = glue::Open(0, "DATA\\GTA.DAT", gtasa::kAccessRead, &h);
    CHECK_EQ_INT(rc, 0);
    CHECK_EQ_STR(glue::openCalls.back(), "/Android/data/com.test.glue/files/modloader/.data/cache/gta.dat");
    std::string merged = ReadWhole(ml + "/.data/cache/gta.dat");
    CHECK(merged.find("IMG MODELS\\GTA_INT.IMG\n") == 0);
    CHECK(merged.find("IDE modloader/.vfs/m001.ide\n") != std::string::npos);
    CHECK(merged.find("IDE modloader/.vfs/m001.ide") < merged.find("IPL DATA"));

    // ---- std.stream ----
    StdStream& S = StdStream::Get();
    CHECK(S.Patcher().IsEnabled());
    CHECK_EQ_INT(S.Layout().entries.size(), 2);
    {
        std::vector<std::tuple<std::string, uint32_t, uint32_t>> entries;
        for(auto& e : S.Layout().entries) entries.push_back(std::make_tuple(e.name, e.offset, e.sectors));
        glue::game.directories[StdStream::Alias()] = FakeGame::BuildDirectory(entries);
    }

    // CStreaming::Init2 loads the directory of every image, through the hooked function
    auto loadHook = (void(*)(const char*, int))glue::hooks[(void*)&glue::LoadCdDirectory];
    for(int i = 0; glue::game.files[i].name[0] != '\0'; ++i)
        if(glue::game.files[i].notPlayerImg)
            loadHook(glue::game.files[i].name, i);

    CHECK_EQ_STR(glue::game.files[2].name, "modloader/.vfs/stream.img");
    CHECK_EQ_INT(glue::game.infos[400].imgId, 2);                                                  // infernus.dff now streams from our archive
    CHECK_EQ_INT(glue::game.infos[400].cdPosn, S.Layout().Find("infernus.dff")->offset);
    CHECK_EQ_INT(glue::game.infos[400].cdSize, 3);
    CHECK_EQ_INT(glue::game.infos[20010].imgId, 2);                                                // vehicle.txd too
    CHECK_EQ_INT(glue::game.infos[401].imgId, 0);                                                  // other.dff untouched
    CHECK_EQ_INT(glue::game.bufferSize, 20);                                                       // unchanged: the replacements are smaller than the biggest original entry (20 sectors)

    // the archive is reachable through the hook of OS_FileOpen (the game opens it by its alias)
    std::string archive = ml + "/.data/cache/stream.img";
    glue::absFiles["/Android/data/com.test.glue/files/modloader/.data/cache/stream.img"] = "x";
    glue::openCalls.clear(); h = nullptr;
    CHECK_EQ_INT(glue::Open(0, StdStream::Alias(), gtasa::kAccessRead, &h), 0);
    CHECK_EQ_STR(glue::openCalls[0], "/Android/data/com.test.glue/files/modloader/.data/cache/stream.img");
    CHECK(fs::Exists(archive));

    // a build of the game whose OS_FileOpen does not look like the expected one gets no hook at all
    gtasa::SetFingerprintChecks(true);
    glue::hooks.clear();
    L.ResetForTests();
    StdData::Get().Reset();
    StdStream::Get().Reset();
    CHECK(L.Init(root));
    L.RegisterPlugin(StdData::Get().Plugin());
    L.RegisterPlugin(StdStream::Get().Plugin());
    L.Start();                                      // the fake functions are not Thumb code: fingerprints cannot match
    CHECK(glue::hooks.empty());
    CHECK(!StdData::Get().IsHooked());
    CHECK(!StdStream::Get().Patcher().IsEnabled());

    gtasa::SetFingerprintChecks(true);
    SetHost(nullptr);
    L.Shutdown();
}

/* ------------------------------------------------------------------------------------------------------------- */
int main()
{
    g_tmp = MakeTempRoot();
    std::printf("temp dir: %s\n", g_tmp.c_str());

    TestStrings();
    TestConfig();
    TestGtaDat();
    TestVfs();
    TestIm();
    TestStreamPatcherEarly();
    TestStreamPatcherLate();
    TestStreamPatcherFailures();
    TestLoaderAndPlugins();
    TestHostGlue();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    int rc = system(("rm -rf '" + g_tmp + "'").c_str());
    (void)rc;
    return g_failures == 0? 0 : 1;
}
