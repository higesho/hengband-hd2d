// 生成物: tools/dcss/gen_terrain_table.py（元: dcss/terrain/feature_map.csv）。手で直さない。
// DCSS の dungeon_feature_type の値 → {HD2D の地形番号, feature_flags, ミニマップの種別}。
static constexpr int kDcssFeatureCount = 221;
static const DcssTerrainRow kDcssTerrain[kDcssFeatureCount] = {
    { 0, static_cast<std::uint16_t>(0), MinimapKind::Unknown }, // 0 DNGN_UNSEEN
    { 2001, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_KNOWN), MinimapKind::Door }, // 1 DNGN_CLOSED_DOOR
    { 2002, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_KNOWN), MinimapKind::Door }, // 2 DNGN_RUNED_DOOR
    { 2003, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_KNOWN), MinimapKind::Door }, // 3 DNGN_SEALED_DOOR
    { 2004, static_cast<std::uint16_t>(CELL_FEAT_TREE | CELL_FEAT_KNOWN), MinimapKind::Tree }, // 4 DNGN_TREE
    { 2005, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 5 DNGN_METAL_WALL
    { 2006, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 6 DNGN_CRYSTAL_WALL
    { 2007, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 7 DNGN_ROCK_WALL
    { 2008, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 8 DNGN_SLIMY_WALL
    { 2009, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 9 DNGN_STONE_WALL
    { 2010, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_PERMANENT | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 10 DNGN_PERMAROCK_WALL
    { 2011, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 11 DNGN_CLEAR_ROCK_WALL
    { 2012, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 12 DNGN_CLEAR_STONE_WALL
    { 2013, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_PERMANENT | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 13 DNGN_CLEAR_PERMAROCK_WALL
    { 2014, static_cast<std::uint16_t>(CELL_FEAT_KNOWN), MinimapKind::Wall }, // 14 DNGN_GRATE
    { 2015, static_cast<std::uint16_t>(CELL_FEAT_WATER | CELL_FEAT_KNOWN), MinimapKind::Water }, // 15 DNGN_OPEN_SEA
    { 2016, static_cast<std::uint16_t>(CELL_FEAT_LAVA | CELL_FEAT_GLOW | CELL_FEAT_KNOWN), MinimapKind::Lava }, // 16 DNGN_LAVA_SEA
    { 2017, static_cast<std::uint16_t>(CELL_FEAT_KNOWN), MinimapKind::Wall }, // 17 DNGN_ORCISH_IDOL
    { 2018, static_cast<std::uint16_t>(CELL_FEAT_KNOWN), MinimapKind::Wall }, // 18 DNGN_GRANITE_STATUE
    { 2019, static_cast<std::uint16_t>(CELL_FEAT_KNOWN), MinimapKind::Wall }, // 19 DNGN_MALIGN_GATEWAY
    { 0, 0, MinimapKind::Unknown }, // 20
    { 0, 0, MinimapKind::Unknown }, // 21
    { 0, 0, MinimapKind::Unknown }, // 22
    { 0, 0, MinimapKind::Unknown }, // 23
    { 0, 0, MinimapKind::Unknown }, // 24
    { 0, 0, MinimapKind::Unknown }, // 25
    { 0, 0, MinimapKind::Unknown }, // 26
    { 0, 0, MinimapKind::Unknown }, // 27
    { 0, 0, MinimapKind::Unknown }, // 28
    { 0, 0, MinimapKind::Unknown }, // 29
    { 2030, static_cast<std::uint16_t>(CELL_FEAT_LAVA | CELL_FEAT_GLOW | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Lava }, // 30 DNGN_LAVA
    { 2031, static_cast<std::uint16_t>(CELL_FEAT_WATER | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Water }, // 31 DNGN_DEEP_WATER
    { 2032, static_cast<std::uint16_t>(CELL_FEAT_WATER | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Water }, // 32 DNGN_SHALLOW_WATER
    { 2033, static_cast<std::uint16_t>(CELL_FEAT_KNOWN | CELL_FEAT_PASSABLE), MinimapKind::Floor }, // 33 DNGN_FLOOR
    { 2034, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_DOOR_OPEN | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Door }, // 34 DNGN_OPEN_DOOR
    { 2035, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 35 DNGN_TRAP_MECHANICAL
    { 2036, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 36 DNGN_TRAP_TELEPORT
    { 2037, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 37 DNGN_TRAP_SHAFT
    { 2038, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 38 DNGN_TRAP_WEB
    { 1, static_cast<std::uint16_t>(CELL_FEAT_KNOWN | CELL_FEAT_PASSABLE), MinimapKind::Floor }, // 39 DNGN_UNDISCOVERED_TRAP
    { 2040, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 40 DNGN_ENTER_SHOP
    { 2041, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 41 DNGN_ABANDONED_SHOP
    { 2042, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 42 DNGN_STONE_STAIRS_DOWN_I
    { 2043, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 43 DNGN_STONE_STAIRS_DOWN_II
    { 2044, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 44 DNGN_STONE_STAIRS_DOWN_III
    { 2045, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 45 DNGN_ESCAPE_HATCH_DOWN
    { 2046, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 46 DNGN_STONE_STAIRS_UP_I
    { 2047, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 47 DNGN_STONE_STAIRS_UP_II
    { 2048, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 48 DNGN_STONE_STAIRS_UP_III
    { 2049, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 49 DNGN_ESCAPE_HATCH_UP
    { 2050, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 50 DNGN_ENTER_DIS
    { 2051, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 51 DNGN_ENTER_GEHENNA
    { 2052, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 52 DNGN_ENTER_COCYTUS
    { 2053, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 53 DNGN_ENTER_TARTARUS
    { 2054, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 54 DNGN_ENTER_ABYSS
    { 2055, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 55 DNGN_EXIT_ABYSS
    { 2056, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 56 DNGN_STONE_ARCH
    { 2057, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 57 DNGN_ENTER_PANDEMONIUM
    { 2058, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 58 DNGN_EXIT_PANDEMONIUM
    { 2059, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 59 DNGN_TRANSIT_PANDEMONIUM
    { 2060, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 60 DNGN_EXIT_DUNGEON
    { 2061, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 61 DNGN_EXIT_THROUGH_ABYSS
    { 2062, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 62 DNGN_EXIT_HELL
    { 2063, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 63 DNGN_ENTER_HELL
    { 2064, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 64 DNGN_ENTER_LABYRINTH
    { 2065, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 65 DNGN_TELEPORTER
    { 2066, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 66 DNGN_ENTER_PORTAL_VAULT
    { 2067, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 67 DNGN_EXIT_PORTAL_VAULT
    { 2068, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 68 DNGN_EXPIRED_PORTAL
    { 2069, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 69 DNGN_ENTER_DWARF
    { 2070, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 70 DNGN_ENTER_ORC
    { 2071, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 71 DNGN_ENTER_LAIR
    { 2072, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 72 DNGN_ENTER_SLIME
    { 2073, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 73 DNGN_ENTER_VAULTS
    { 2074, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 74 DNGN_ENTER_CRYPT
    { 2075, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 75 DNGN_ENTER_BLADE
    { 2076, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 76 DNGN_ENTER_ZOT
    { 2077, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 77 DNGN_ENTER_TEMPLE
    { 2078, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 78 DNGN_ENTER_SNAKE
    { 2079, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 79 DNGN_ENTER_ELF
    { 2080, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 80 DNGN_ENTER_TOMB
    { 2081, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 81 DNGN_ENTER_SWAMP
    { 2082, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 82 DNGN_ENTER_SHOALS
    { 2083, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 83 DNGN_ENTER_SPIDER
    { 2084, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 84 DNGN_ENTER_FOREST
    { 2085, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 85 DNGN_ENTER_DEPTHS
    { 2086, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 86 DNGN_EXIT_DWARF
    { 2087, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 87 DNGN_EXIT_ORC
    { 2088, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 88 DNGN_EXIT_LAIR
    { 2089, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 89 DNGN_EXIT_SLIME
    { 2090, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 90 DNGN_EXIT_VAULTS
    { 2091, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 91 DNGN_EXIT_CRYPT
    { 2092, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 92 DNGN_EXIT_BLADE
    { 2093, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 93 DNGN_EXIT_ZOT
    { 2094, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 94 DNGN_EXIT_TEMPLE
    { 2095, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 95 DNGN_EXIT_SNAKE
    { 2096, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 96 DNGN_EXIT_ELF
    { 2097, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 97 DNGN_EXIT_TOMB
    { 2098, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 98 DNGN_EXIT_SWAMP
    { 2099, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 99 DNGN_EXIT_SHOALS
    { 2100, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 100 DNGN_EXIT_SPIDER
    { 2101, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 101 DNGN_EXIT_FOREST
    { 2102, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 102 DNGN_EXIT_DEPTHS
    { 2103, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 103 DNGN_ALTAR_ZIN
    { 2104, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 104 DNGN_ALTAR_SHINING_ONE
    { 2105, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 105 DNGN_ALTAR_KIKUBAAQUDGHA
    { 2106, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 106 DNGN_ALTAR_YREDELEMNUL
    { 2107, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 107 DNGN_ALTAR_XOM
    { 2108, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 108 DNGN_ALTAR_VEHUMET
    { 2109, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 109 DNGN_ALTAR_OKAWARU
    { 2110, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 110 DNGN_ALTAR_MAKHLEB
    { 2111, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 111 DNGN_ALTAR_SIF_MUNA
    { 2112, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 112 DNGN_ALTAR_TROG
    { 2113, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 113 DNGN_ALTAR_NEMELEX_XOBEH
    { 2114, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 114 DNGN_ALTAR_ELYVILON
    { 2115, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 115 DNGN_ALTAR_LUGONU
    { 2116, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 116 DNGN_ALTAR_BEOGH
    { 2117, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 117 DNGN_ALTAR_JIYVA
    { 2118, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 118 DNGN_ALTAR_FEDHAS
    { 2119, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 119 DNGN_ALTAR_CHEIBRIADOS
    { 2120, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 120 DNGN_ALTAR_ASHENZARI
    { 2121, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 121 DNGN_ALTAR_DITHMENOS
    { 2122, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 122 DNGN_FOUNTAIN_BLUE
    { 2123, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 123 DNGN_FOUNTAIN_SPARKLING
    { 2124, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 124 DNGN_FOUNTAIN_BLOOD
    { 2125, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 125 DNGN_DRY_FOUNTAIN_BLUE
    { 2126, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 126 DNGN_DRY_FOUNTAIN_SPARKLING
    { 2127, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 127 DNGN_DRY_FOUNTAIN_BLOOD
    { 2128, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 128 DNGN_DRY_FOUNTAIN
    { 0, static_cast<std::uint16_t>(0), MinimapKind::Unknown }, // 129 DNGN_EXPLORE_HORIZON
    { 2130, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 130 DNGN_UNKNOWN_ALTAR
    { 2131, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 131 DNGN_UNKNOWN_PORTAL
    { 2132, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 132 DNGN_ABYSSAL_STAIR
    { 1, static_cast<std::uint16_t>(CELL_FEAT_KNOWN | CELL_FEAT_PASSABLE), MinimapKind::Floor }, // 133 DNGN_BADLY_SEALED_DOOR
    { 2134, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 134 DNGN_SEALED_STAIRS_UP
    { 2135, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 135 DNGN_SEALED_STAIRS_DOWN
    { 2136, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 136 DNGN_TRAP_ALARM
    { 2137, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 137 DNGN_TRAP_ZOT
    { 2138, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 138 DNGN_PASSAGE_OF_GOLUBRIA
    { 2139, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 139 DNGN_ENTER_ZIGGURAT
    { 2140, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 140 DNGN_ENTER_BAZAAR
    { 2141, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 141 DNGN_ENTER_TROVE
    { 2142, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 142 DNGN_ENTER_SEWER
    { 2143, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 143 DNGN_ENTER_OSSUARY
    { 2144, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 144 DNGN_ENTER_BAILEY
    { 2145, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 145 DNGN_ENTER_ICE_CAVE
    { 2146, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 146 DNGN_ENTER_VOLCANO
    { 2147, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 147 DNGN_ENTER_WIZLAB
    { 2148, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 148 DNGN_ENTER_DESOLATION
    { 2149, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 149 DNGN_EXIT_ZIGGURAT
    { 2150, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 150 DNGN_EXIT_BAZAAR
    { 2151, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 151 DNGN_EXIT_TROVE
    { 2152, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 152 DNGN_EXIT_SEWER
    { 2153, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 153 DNGN_EXIT_OSSUARY
    { 2154, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 154 DNGN_EXIT_BAILEY
    { 2155, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 155 DNGN_EXIT_ICE_CAVE
    { 2156, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 156 DNGN_EXIT_VOLCANO
    { 2157, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 157 DNGN_EXIT_WIZLAB
    { 2158, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 158 DNGN_EXIT_LABYRINTH
    { 2159, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 159 DNGN_EXIT_DESOLATION
    { 2160, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 160 DNGN_ALTAR_GOZAG
    { 2161, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 161 DNGN_ALTAR_QAZLAL
    { 2162, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 162 DNGN_ALTAR_RU
    { 1, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 163 DNGN_TRAP_SHADOW
    { 1, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 164 DNGN_TRAP_SHADOW_DORMANT
    { 2165, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 165 DNGN_ALTAR_ECUMENICAL
    { 2166, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 166 DNGN_ALTAR_PAKELLAS
    { 2167, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 167 DNGN_ALTAR_USKAYAW
    { 2168, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 168 DNGN_ALTAR_HEPLIAKLQANA
    { 2169, static_cast<std::uint16_t>(CELL_FEAT_KNOWN), MinimapKind::Floor }, // 169 DNGN_ENDLESS_SALT
    { 2170, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 170 DNGN_ALTAR_WU_JIAN
    { 2171, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 171 DNGN_TRANSPORTER
    { 2172, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 172 DNGN_TRANSPORTER_LANDING
    { 2173, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 173 DNGN_TRAP_DISPERSAL
    { 2174, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 174 DNGN_ENTER_GAUNTLET
    { 2175, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 175 DNGN_EXIT_GAUNTLET
    { 2176, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_KNOWN), MinimapKind::Door }, // 176 DNGN_CLOSED_CLEAR_DOOR
    { 2177, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_DOOR_OPEN | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Door }, // 177 DNGN_OPEN_CLEAR_DOOR
    { 2178, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_KNOWN), MinimapKind::Door }, // 178 DNGN_RUNED_CLEAR_DOOR
    { 2179, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_KNOWN), MinimapKind::Door }, // 179 DNGN_SEALED_CLEAR_DOOR
    { 2180, static_cast<std::uint16_t>(CELL_FEAT_WATER | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Water }, // 180 DNGN_TOXIC_BOG
    { 2181, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 181 DNGN_TRAP_ARCHMAGE
    { 2182, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 182 DNGN_TRAP_SPEAR
    { 2183, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 183 DNGN_TRAP_TYRANT
    { 2184, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 184 DNGN_TRAP_HARLEQUIN
    { 2185, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 185 DNGN_TRAP_BOLT
    { 2186, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 186 DNGN_TRAP_NET
    { 2187, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 187 DNGN_TRAP_PLATE
    { 2188, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 188 DNGN_TRAP_TELEPORT_PERMANENT
    { 0, static_cast<std::uint16_t>(0), MinimapKind::Unknown }, // 189 DNGN_TRAVEL_TRAIL
    { 2190, static_cast<std::uint16_t>(CELL_FEAT_TREE | CELL_FEAT_KNOWN), MinimapKind::Tree }, // 190 DNGN_PETRIFIED_TREE
    { 2191, static_cast<std::uint16_t>(CELL_FEAT_TREE | CELL_FEAT_KNOWN), MinimapKind::Tree }, // 191 DNGN_MANGROVE
    { 2192, static_cast<std::uint16_t>(CELL_FEAT_TREE | CELL_FEAT_KNOWN), MinimapKind::Tree }, // 192 DNGN_DEMONIC_TREE
    { 2193, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 193 DNGN_ENTER_ARENA
    { 2194, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 194 DNGN_EXIT_ARENA
    { 2195, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 195 DNGN_ALTAR_IGNIS
    { 2196, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_DOOR_OPEN | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Door }, // 196 DNGN_BROKEN_DOOR
    { 2197, static_cast<std::uint16_t>(CELL_FEAT_DOOR | CELL_FEAT_DOOR_OPEN | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Door }, // 197 DNGN_BROKEN_CLEAR_DOOR
    { 2198, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 198 DNGN_EXIT_DIS
    { 2199, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 199 DNGN_EXIT_GEHENNA
    { 2200, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 200 DNGN_EXIT_COCYTUS
    { 2201, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 201 DNGN_EXIT_TARTARUS
    { 2202, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 202 DNGN_RUNELIGHT
    { 2203, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 203 DNGN_MUD
    { 2204, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 204 DNGN_BINDING_SIGIL
    { 2205, static_cast<std::uint16_t>(CELL_FEAT_KNOWN), MinimapKind::Wall }, // 205 DNGN_METAL_STATUE
    { 2206, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 206 DNGN_CACHE_OF_FRUIT
    { 2207, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 207 DNGN_CACHE_OF_MEAT
    { 2208, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 208 DNGN_ORB_DAIS
    { 2209, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 209 DNGN_ENTER_CRUCIBLE
    { 2210, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 210 DNGN_EXIT_CRUCIBLE
    { 2211, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 211 DNGN_FOUNTAIN_EYES
    { 2212, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 212 DNGN_CACHE_OF_BAKED_GOODS
    { 2213, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 213 DNGN_DECORATIVE_FLOOR
    { 2214, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 214 DNGN_SPIKE_LAUNCHER
    { 2215, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 215 DNGN_TRAP_DEVOURER
    { 2216, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 216 DNGN_ENTER_NECROPOLIS
    { 2217, static_cast<std::uint16_t>(CELL_FEAT_STAIRS | CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Stairs }, // 217 DNGN_EXIT_NECROPOLIS
    { 2218, static_cast<std::uint16_t>(CELL_FEAT_WALL | CELL_FEAT_KNOWN), MinimapKind::Wall }, // 218 DNGN_FRIGID_WALL
    { 2219, static_cast<std::uint16_t>(CELL_FEAT_KNOWN), MinimapKind::Wall }, // 219 DNGN_ZOT_STATUE
    { 2220, static_cast<std::uint16_t>(CELL_FEAT_PASSABLE | CELL_FEAT_KNOWN), MinimapKind::Floor }, // 220 DNGN_TRAP_DISPERSAL_INACTIVE
};
