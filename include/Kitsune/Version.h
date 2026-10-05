#pragma once

#include <string_view>

namespace kitsune {

// Official client this library is aiming at. Linux Base75689 and the Windows
// executable of that same 4.10 build are the pair. Newer Windows builds are
// reference material only.
//
// The versions.json record for that build, and the Version column of the
// install's .build.info. Ping reports game_version from .build.info,
// data_version as the data-hash, data_build as version, and base_build as
// base-version. fixed-hash and replay-hash are the build-config keys the
// client reads. They are not ResponsePing fields.
inline constexpr int kTargetMajor = 4;
inline constexpr int kTargetMinor = 10;
inline constexpr int kTargetBuild = 75689;
inline constexpr int kDataBuild = 75689;
inline constexpr int kBaseBuild = 75689;

inline constexpr std::string_view kTargetLabel = "4.10";
inline constexpr std::string_view kGameVersion = "4.10.0.75689";
inline constexpr std::string_view kDataHash = "B89B5D6FA7CBF6452E721311BFBC6CB2";
inline constexpr std::string_view kFixedHash = "2B2097DC4AD60A2D1E1F38691A1FF111";
inline constexpr std::string_view kReplayHash = "6A60E59031A7DB1B272EE87E51E4C7CD";

// Library version. Independent of kTargetBuild: the client build we match
// does not move when the library does.
inline constexpr std::string_view kLibraryVersion = "0.0.1";

// .build.info Version column. This is the string ResponsePing.game_version uses.
std::string_view targetLabel() noexcept;

} // namespace kitsune
