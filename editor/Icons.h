#pragma once

// UTF-8 literals for the Lucide glyphs merged into the editor fonts (Palette.cpp).
// Codepoints come from the font's published codepoint table.

namespace rb::editor::icon {

// codepoint window the atlas build covers (min..max of the glyphs below)
inline constexpr unsigned short kRangeBegin = 0xE061;
inline constexpr unsigned short kRangeEnd = 0xE529;

inline constexpr const char* kPlay = "\xEE\x84\xBC";
inline constexpr const char* kPause = "\xEE\x84\xAE";
inline constexpr const char* kStepForward = "\xEE\x8F\xAA";
inline constexpr const char* kSquare = "\xEE\x85\xA7";
inline constexpr const char* kSquareDashed = "\xEE\x87\x8B";
inline constexpr const char* kBox = "\xEE\x81\xA1";
inline constexpr const char* kBoxes = "\xEE\x8B\x90";
inline constexpr const char* kCircle = "\xEE\x81\xB6";
inline constexpr const char* kFrame = "\xEE\x8A\x91";
inline constexpr const char* kSun = "\xEE\x85\xB8";
inline constexpr const char* kLightbulb = "\xEE\x87\x82";
inline constexpr const char* kFlashlight = "\xEE\x83\x93";
inline constexpr const char* kVideo = "\xEE\x86\xA5";
inline constexpr const char* kSparkles = "\xEE\x90\x92";
inline constexpr const char* kWandSparkles = "\xEE\x8D\x97";
inline constexpr const char* kSlidersHorizontal = "\xEE\x8A\x9A";
inline constexpr const char* kMountain = "\xEE\x88\xB1";
inline constexpr const char* kFileCode = "\xEE\x83\x83";
inline constexpr const char* kVolume = "\xEE\x86\xAB";
inline constexpr const char* kImage = "\xEE\x83\xB6";
inline constexpr const char* kPackage = "\xEE\x84\xA9";
inline constexpr const char* kGhost = "\xEE\x88\x8E";
inline constexpr const char* kLayers = "\xEE\x94\xA9";
inline constexpr const char* kPlus = "\xEE\x84\xBD";
inline constexpr const char* kX = "\xEE\x86\xB2";
inline constexpr const char* kSearch = "\xEE\x85\x91";
inline constexpr const char* kFolder = "\xEE\x83\x97";
inline constexpr const char* kFolderOpen = "\xEE\x89\x87";
inline constexpr const char* kLayoutGrid = "\xEE\x83\xBF";
inline constexpr const char* kList = "\xEE\x84\x86";
inline constexpr const char* kChevronRight = "\xEE\x81\xAF";
inline constexpr const char* kSave = "\xEE\x85\x8D";
inline constexpr const char* kTrash = "\xEE\x86\x8E";
inline constexpr const char* kCopy = "\xEE\x82\x9E";
inline constexpr const char* kLink = "\xEE\x84\x82";
inline constexpr const char* kRotateCcw = "\xEE\x85\x88";
inline constexpr const char* kMove3d = "\xEE\x8B\xA5";
inline constexpr const char* kRotate3d = "\xEE\x8B\xAA";
inline constexpr const char* kScale3d = "\xEE\x8B\xAB";
inline constexpr const char* kMagnet = "\xEE\x8A\xB5";
inline constexpr const char* kEye = "\xEE\x82\xBA";
inline constexpr const char* kGlobe = "\xEE\x83\xA8";
inline constexpr const char* kCamera = "\xEE\x81\xA4";
inline constexpr const char* kBug = "\xEE\x88\x8C";
inline constexpr const char* kInfo = "\xEE\x83\xB9";
inline constexpr const char* kTriangleAlert = "\xEE\x86\x93";
inline constexpr const char* kCircleX = "\xEE\x82\x84";
inline constexpr const char* kFile = "\xEE\x83\x80";
inline constexpr const char* kMusic = "\xEE\x84\xA2";
inline constexpr const char* kGrid = "\xEE\x83\xA9";
inline constexpr const char* kAxis3d = "\xEE\x8B\xBE";
inline constexpr const char* kTag = "\xEE\x85\xBF";
inline constexpr const char* kAtom = "\xEE\x8F\x97";
inline constexpr const char* kCircleDot = "\xEE\x8D\x85";

} // namespace rb::editor::icon
