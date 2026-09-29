#pragma once

#include <cstddef>
#include <cstdint>

namespace touchcal {

struct Point { double x, y; };
struct Affine { double a, b, c, d, e, f; };
struct Residual { double dx, dy; };
enum class TargetShape : uint8_t { FilledCircle, Ring, Hexagon, Asteroid };
struct AsteroidTarget { Point center; TargetShape shape; uint8_t visualRadius; uint32_t rgb; uint8_t radialLayer; uint8_t sector; };
struct WarpMap { Affine affine; Residual center; Residual rings[3][16]; };
struct SampleResult { bool accepted; Point median; };
struct Verification { double meanError, maxError; bool passed; };
struct PerimeterCoverage { size_t sectorsCovered; size_t totalSamples; bool complete; };

struct Record {
  uint32_t magic;
  uint16_t version;
  uint16_t width;
  uint16_t height;
  uint8_t rotation;
  uint8_t reserved;
  float coefficients[6];
  uint32_t checksum;
};
struct RecordV2 {
  uint32_t magic; uint16_t version, width, height;
  uint8_t rotation, angularSectors, radialRings, reserved;
  uint32_t generation; float affine[6]; float centerResidual[2];
  float residuals[3][16][2]; uint32_t checksum;
};

inline constexpr int kDisplayWidth = 468;
inline constexpr int kDisplayHeight = 466;
inline constexpr int kRotation = 0;
inline constexpr size_t kCardinalCount = 4;
inline constexpr Point kCenter = {234, 233};
inline constexpr double kPerimeterRadius = 170.0;
inline constexpr size_t kPerimeterSectorCount = 16;
inline constexpr size_t kSamplesPerPerimeterSector = 4;
inline constexpr size_t kRadialRingCount = 3;
inline constexpr size_t kAsteroidAttemptCount = 51;
inline constexpr size_t kCenterAttemptCount = 3;
inline constexpr double kMaximumResidualMagnitude = 48.0;
inline constexpr double kMaximumNeighborDelta = 32.0;
inline constexpr size_t kMinimumSamples = 16;
inline constexpr uint32_t kMinimumSampleDurationMs = 80;
inline constexpr double kMaximumSampleSpan = 12.0;
inline constexpr double kMaximumMeanError = 12.0;
inline constexpr double kMaximumPointError = 22.0;
inline constexpr uint32_t kRecordMagic = 0x5443414c;
inline constexpr uint16_t kRecordVersion = 1;
inline constexpr uint16_t kRecordVersionV2 = 2;
inline constexpr char kNvsNamespace[] = "espt-touch";

extern const Point kCardinalTargets[kCardinalCount];
extern const double kRadialRings[kRadialRingCount];

SampleResult reduceSamples(const Point* samples, size_t count, uint32_t durationMs);
SampleResult reduceAttempt(const Point* samples, size_t count);
bool asteroidTarget(size_t attempt, AsteroidTarget* target);
bool fitAffine(const Point* raw, const Point* screen, size_t count, Affine* result);
Point apply(const Affine& transform, Point raw);
bool validAffine(const Affine& transform);
WarpMap zeroWarp(const Affine& transform);
Point applyWarp(const WarpMap& map, Point raw);
bool validWarp(const WarpMap& map);
void smoothRing(const Residual input[16], Residual output[16]);
void averageRings(const Residual clockwise[16], const Residual counterclockwise[16], Residual output[16]);
Verification verify(const Point* expected, const Point* observed, size_t count);
size_t perimeterSector(Point point);
PerimeterCoverage analyzePerimeter(const Point* observed, size_t count);
uint32_t checksumRecord(const Record& record);
Record makeRecord(const Affine& transform);
bool validateRecord(const Record& record);
uint32_t checksumRecordV2(const RecordV2& record);
RecordV2 makeRecordV2(const WarpMap& map, uint32_t generation);
bool validateRecordV2(const RecordV2& record);
WarpMap warpFromV1(const Record& record);

}  // namespace touchcal

static_assert(sizeof(touchcal::Record) == 40, "Calibration record layout changed");
static_assert(sizeof(touchcal::RecordV2) == 440, "Calibration v2 record layout changed");
