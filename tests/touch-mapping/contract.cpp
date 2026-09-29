#include "touch_calibration_core.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int assertions = 0;
int failures = 0;

#define CHECK(condition) do { \
  ++assertions; \
  if (!(condition)) { \
    ++failures; \
    std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
  } \
} while (false)

bool close(double actual, double expected, double tolerance = 1e-6) {
  return std::abs(actual - expected) <= tolerance;
}

void checkPoint(touchcal::Point actual, touchcal::Point expected, double tolerance = 1e-6) {
  CHECK(close(actual.x, expected.x, tolerance));
  CHECK(close(actual.y, expected.y, tolerance));
}

void checkAffine(const touchcal::Affine& actual, const touchcal::Affine& expected,
                 double tolerance = 1e-6) {
  CHECK(close(actual.a, expected.a, tolerance));
  CHECK(close(actual.b, expected.b, tolerance));
  CHECK(close(actual.c, expected.c, tolerance));
  CHECK(close(actual.d, expected.d, tolerance));
  CHECK(close(actual.e, expected.e, tolerance));
  CHECK(close(actual.f, expected.f, tolerance));
}

uint32_t crcByte(uint32_t crc, uint8_t value) {
  crc ^= value;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return crc;
}

void crcScalar(uint32_t& crc, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    crc = crcByte(crc, static_cast<uint8_t>(value >> (8 * i)));
  }
}

uint32_t expectedChecksum(const touchcal::Record& record) {
  uint32_t crc = 0xffffffffu;
  crcScalar(crc, record.magic, 4);
  crcScalar(crc, record.version, 2);
  crcScalar(crc, record.width, 2);
  crcScalar(crc, record.height, 2);
  crcScalar(crc, record.rotation, 1);
  crcScalar(crc, record.reserved, 1);
  for (float coefficient : record.coefficients) {
    uint32_t bits = 0;
    std::memcpy(&bits, &coefficient, sizeof(bits));
    crcScalar(crc, bits, 4);
  }
  return crc ^ 0xffffffffu;
}

uint32_t expectedChecksumV2(const touchcal::RecordV2& record) {
  uint32_t crc = 0xffffffffu;
  crcScalar(crc, record.magic, 4); crcScalar(crc, record.version, 2);
  crcScalar(crc, record.width, 2); crcScalar(crc, record.height, 2);
  crcScalar(crc, record.rotation, 1); crcScalar(crc, record.angularSectors, 1);
  crcScalar(crc, record.radialRings, 1); crcScalar(crc, record.reserved, 1);
  crcScalar(crc, record.generation, 4);
  auto feedFloat = [&](float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    crcScalar(crc, bits, 4);
  };
  for (float value : record.affine) feedFloat(value);
  for (float value : record.centerResidual) feedFloat(value);
  for (const auto& ring : record.residuals)
    for (const auto& sector : ring)
      for (float value : sector) feedFloat(value);
  return crc ^ 0xffffffffu;
}

void checkConstants() {
  using namespace touchcal;
  CHECK(kDisplayWidth == 468);
  CHECK(kDisplayHeight == 466);
  CHECK(kRotation == 0);
  CHECK(kCardinalCount == 4);
  checkPoint(kCenter, {234, 233});
  CHECK(close(kPerimeterRadius, 170.0));
  CHECK(kPerimeterSectorCount == 16);
  CHECK(kSamplesPerPerimeterSector == 4);
  CHECK(kRadialRingCount == 3);
  CHECK(kAsteroidAttemptCount == 51);
  CHECK(kCenterAttemptCount == 3);
  CHECK(close(kRadialRings[0], 70));
  CHECK(close(kRadialRings[1], 120));
  CHECK(close(kRadialRings[2], 170));
  CHECK(close(kMaximumResidualMagnitude, 48));
  CHECK(close(kMaximumNeighborDelta, 32));
  CHECK(kMinimumSamples == 16);
  CHECK(kMinimumSampleDurationMs == 80);
  CHECK(close(kMaximumSampleSpan, 12.0));
  CHECK(close(kMaximumMeanError, 12.0));
  CHECK(close(kMaximumPointError, 22.0));
  CHECK(kRecordMagic == 0x5443414cu);
  CHECK(kRecordVersion == 1);
  CHECK(kRecordVersionV2 == 2);
  CHECK(std::strcmp(kNvsNamespace, "espt-touch") == 0);
  CHECK(std::strlen(kNvsNamespace) <= 15);
  CHECK(sizeof(Record) == 40);
  CHECK(sizeof(RecordV2) == 440);

  const Point expected[] = {{234, 63}, {234, 403}, {64, 233}, {404, 233}};
  for (size_t i = 0; i < kCardinalCount; ++i) {
    checkPoint(kCardinalTargets[i], expected[i]);
    CHECK(close(std::hypot(kCardinalTargets[i].x - kCenter.x,
                           kCardinalTargets[i].y - kCenter.y), kPerimeterRadius));
  }
}

void checkAsteroidTargets() {
  using namespace touchcal;
  constexpr double pi = 3.14159265358979323846;
  constexpr uint8_t expectedRadii[] = {10, 14, 18, 22};
  constexpr uint32_t expectedColors[] = {
    0xff7a00, 0x00d9ff, 0x9dff00, 0xff3bd4, 0xffffff,
  };
  bool locations[4][16]{};
  bool shapes[4]{};
  bool radii[4]{};
  bool colors[5]{};
  size_t centerCount = 0;

  for (size_t attempt = 0; attempt < kAsteroidAttemptCount; ++attempt) {
    AsteroidTarget target{};
    CHECK(asteroidTarget(attempt, &target));
    const size_t id = (attempt * 20 + 7) % kAsteroidAttemptCount;
    CHECK(static_cast<size_t>(target.shape) == attempt % 4);
    CHECK(target.visualRadius == expectedRadii[(attempt * 3 + 1) % 4]);
    CHECK(target.rgb == expectedColors[(attempt * 2 + 3) % 5]);
    shapes[static_cast<size_t>(target.shape)] = true;
    radii[(attempt * 3 + 1) % 4] = true;
    colors[(attempt * 2 + 3) % 5] = true;

    if (id < 3) {
      CHECK(target.radialLayer == 0);
      CHECK(target.sector == 255);
      checkPoint(target.center, kCenter);
      ++centerCount;
    } else {
      const uint8_t layer = id < 19 ? 1 : id < 35 ? 2 : 3;
      const uint8_t sector = static_cast<uint8_t>(id < 19 ? id - 3 : id < 35 ? id - 19 : id - 35);
      const double radius = kRadialRings[layer - 1];
      const double angle = sector * 2.0 * pi / kPerimeterSectorCount;
      CHECK(target.radialLayer == layer);
      CHECK(target.sector == sector);
      checkPoint(target.center,
                 {kCenter.x + std::sin(angle) * radius,
                  kCenter.y - std::cos(angle) * radius});
      CHECK(!locations[layer][sector]);
      locations[layer][sector] = true;
    }
  }

  CHECK(centerCount == 3);
  for (size_t layer = 1; layer <= 3; ++layer)
    for (bool present : locations[layer]) CHECK(present);
  for (bool present : shapes) CHECK(present);
  for (bool present : radii) CHECK(present);
  for (bool present : colors) CHECK(present);
  AsteroidTarget unused{};
  CHECK(!asteroidTarget(kAsteroidAttemptCount, &unused));
  CHECK(!asteroidTarget(0, nullptr));

  const Point quickMiss[] = {{19, 421}};
  auto reduced = reduceAttempt(quickMiss, 1);
  CHECK(reduced.accepted);
  checkPoint(reduced.median, quickMiss[0]);

  const Point contact[] = {
    {100, 200}, {104, 204}, {102, 202},
    {std::numeric_limits<double>::quiet_NaN(), 9},
  };
  reduced = reduceAttempt(contact, 4);
  CHECK(reduced.accepted);
  checkPoint(reduced.median, {102, 202});
  const Point invalid[] = {{std::numeric_limits<double>::infinity(), 1}};
  CHECK(!reduceAttempt(invalid, 1).accepted);
  CHECK(!reduceAttempt(nullptr, 1).accepted);
  CHECK(!reduceAttempt(quickMiss, 0).accepted);
}

void checkSamples() {
  using namespace touchcal;
  std::array<Point, 16> stable{};
  for (size_t i = 0; i < stable.size(); ++i) {
    stable[i] = {100.0 + static_cast<double>(i % 7),
                 200.0 + static_cast<double>(i % 5)};
  }
  auto result = reduceSamples(stable.data(), stable.size(), 80);
  CHECK(result.accepted);
  checkPoint(result.median, {102.5, 202.0});

  CHECK(!reduceSamples(stable.data(), 15, 80).accepted);
  CHECK(!reduceSamples(stable.data(), stable.size(), 79).accepted);

  auto wide = stable;
  wide.back().x = 113;
  CHECK(!reduceSamples(wide.data(), wide.size(), 80).accepted);
  wide = stable;
  wide.back().y = 213;
  CHECK(!reduceSamples(wide.data(), wide.size(), 80).accepted);

  auto invalid = stable;
  invalid[3].x = std::numeric_limits<double>::quiet_NaN();
  CHECK(!reduceSamples(invalid.data(), invalid.size(), 80).accepted);
  CHECK(!reduceSamples(nullptr, stable.size(), 80).accepted);

  auto boundary = stable;
  for (auto& point : boundary) point = {100, 200};
  boundary.back() = {112, 212};
  CHECK(reduceSamples(boundary.data(), boundary.size(), 80).accepted);
}

void checkFit(const touchcal::Affine& expected, double tolerance = 1e-6) {
  using namespace touchcal;
  const Point raw[] = {{110, 90}, {870, 105}, {910, 850}, {85, 890}, {505, 490}};
  Point screen[5];
  for (size_t i = 0; i < 5; ++i) screen[i] = apply(expected, raw[i]);
  Affine fitted{};
  CHECK(fitAffine(raw, screen, 5, &fitted));
  CHECK(validAffine(fitted));
  checkAffine(fitted, expected, tolerance);
  checkPoint(apply(fitted, {333, 777}), apply(expected, {333, 777}), tolerance * 100);
}

void checkFits() {
  using namespace touchcal;
  checkFit({1, 0, 0, 0, 1, 0});
  checkFit({0.5, 0, 10, 0, 0.45, 8});
  checkFit({0, 0.4, 5, 0.42, 0, 7});
  checkFit({-0.45, 0, 460, 0, -0.44, 450});
  checkFit({0.4, 0.03, 4, -0.02, 0.43, 5});

  const Point raw[] = {{100, 100}, {900, 100}, {900, 900}, {100, 900}, {500, 500}};
  const Affine expected{0.4, 0.02, 8, -0.01, 0.42, 9};
  const Point noise[] = {{1, -1}, {-1, 1}, {1.5, 0.5}, {-0.5, -1.5}, {0.25, 0.5}};
  Point screen[5];
  for (size_t i = 0; i < 5; ++i) {
    screen[i] = apply(expected, raw[i]);
    screen[i].x += noise[i].x;
    screen[i].y += noise[i].y;
  }
  Affine fitted{};
  CHECK(fitAffine(raw, screen, 5, &fitted));
  Point corrected[5];
  Point exact[5];
  for (size_t i = 0; i < 5; ++i) {
    corrected[i] = apply(fitted, raw[i]);
    exact[i] = apply(expected, raw[i]);
  }
  const auto metrics = verify(exact, corrected, 5);
  CHECK(metrics.passed);
  CHECK(metrics.maxError < 2.0);

  const Point line[] = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
  CHECK(!fitAffine(line, line, 5, &fitted));
  CHECK(!fitAffine(raw, screen, 3, &fitted));
  CHECK(!fitAffine(nullptr, screen, 5, &fitted));
  CHECK(!fitAffine(raw, nullptr, 5, &fitted));
  CHECK(!fitAffine(raw, screen, 5, nullptr));

  CHECK(validAffine({1, 0, 0, 0, 1, 0}));
  CHECK(!validAffine({1, 2, 0, 2, 4, 0}));
  CHECK(!validAffine({std::numeric_limits<double>::infinity(), 0, 0, 0, 1, 0}));
  CHECK(validAffine({1e-9, 0, 0, 0, 1, 0}));
  CHECK(!validAffine({0.999e-9, 0, 0, 0, 1, 0}));
}

uint32_t randomState = 0x7192026u;

double nextRandom(double low, double high) {
  randomState = randomState * 1664525u + 1013904223u;
  const double unit = static_cast<double>(randomState) / std::numeric_limits<uint32_t>::max();
  return low + (high - low) * unit;
}

void checkGeneratedFits() {
  using namespace touchcal;
  for (int trial = 0; trial < 100; ++trial) {
    const Point raw[] = {
      {nextRandom(0, 800), nextRandom(0, 800)},
      {nextRandom(3200, 4095), nextRandom(0, 800)},
      {nextRandom(3200, 4095), nextRandom(3200, 4095)},
      {nextRandom(0, 800), nextRandom(3200, 4095)},
      {nextRandom(1500, 2600), nextRandom(1500, 2600)},
    };
    const Affine expected{
      nextRandom(0.2, 0.8), nextRandom(-0.08, 0.08), nextRandom(-50, 50),
      nextRandom(-0.08, 0.08), nextRandom(0.2, 0.8), nextRandom(-50, 50),
    };
    Point screen[5];
    for (size_t i = 0; i < 5; ++i) screen[i] = apply(expected, raw[i]);
    Affine fitted{};
    CHECK(fitAffine(raw, screen, 5, &fitted));
    const Point probe{nextRandom(0, 4095), nextRandom(0, 4095)};
    checkPoint(apply(fitted, probe), apply(expected, probe), 1e-5);
  }

  const Point fourRaw[] = {{0, 0}, {4095, 0}, {4095, 4095}, {0, 4095}};
  const Affine expected{0.11, 0.002, 5, -0.003, 0.109, 7};
  Point fourScreen[4];
  for (size_t i = 0; i < 4; ++i) fourScreen[i] = apply(expected, fourRaw[i]);
  Affine fitted{};
  CHECK(fitAffine(fourRaw, fourScreen, 4, &fitted));
  checkAffine(fitted, expected, 1e-8);
}

void checkVerification() {
  using namespace touchcal;
  const Point expected[] = {{0, 0}, {10, 10}, {20, 20}, {30, 30}, {40, 40}};
  Point observed[5];
  for (size_t i = 0; i < 5; ++i) observed[i] = {expected[i].x + 3, expected[i].y + 4};
  auto result = verify(expected, observed, 5);
  CHECK(result.passed);
  CHECK(close(result.meanError, 5));
  CHECK(close(result.maxError, 5));

  std::copy(std::begin(expected), std::end(expected), observed);
  observed[0].x += 22;
  CHECK(verify(expected, observed, 5).passed);
  observed[0].x += 0.001;
  CHECK(!verify(expected, observed, 5).passed);

  for (size_t i = 0; i < 5; ++i) observed[i] = {expected[i].x + 13, expected[i].y};
  CHECK(!verify(expected, observed, 5).passed);
  for (size_t i = 0; i < 5; ++i) observed[i] = {expected[i].x + 12, expected[i].y};
  CHECK(verify(expected, observed, 5).passed);
  CHECK(!verify(expected, observed, 0).passed);
  CHECK(!verify(nullptr, observed, 5).passed);
}

std::vector<touchcal::Point> circleTrace(double radius, bool clockwise = true) {
  using namespace touchcal;
  constexpr double pi = 3.14159265358979323846;
  std::vector<Point> points;
  points.reserve(kPerimeterSectorCount * kSamplesPerPerimeterSector);
  const double offsets[] = {-6.0, -2.0, 2.0, 6.0};
  for (size_t step = 0; step < kPerimeterSectorCount; ++step) {
    const size_t sector = clockwise ? step : (kPerimeterSectorCount - step) % kPerimeterSectorCount;
    for (double offset : offsets) {
      const double angle = (sector * 22.5 + offset) * pi / 180.0;
      points.push_back({kCenter.x + std::sin(angle) * radius,
                        kCenter.y - std::cos(angle) * radius});
    }
  }
  return points;
}

void checkPerimeterCollection() {
  using namespace touchcal;
  CHECK(perimeterSector({kCenter.x, kCenter.y - 100}) == 0);
  CHECK(perimeterSector({kCenter.x + 100, kCenter.y}) == 4);
  CHECK(perimeterSector({kCenter.x, kCenter.y + 100}) == 8);
  CHECK(perimeterSector({kCenter.x - 100, kCenter.y}) == 12);
  CHECK(perimeterSector({std::numeric_limits<double>::quiet_NaN(), 0}) == 16);

  auto exact = circleTrace(kPerimeterRadius);
  auto result = analyzePerimeter(exact.data(), exact.size());
  CHECK(result.complete);
  CHECK(result.sectorsCovered == 16);
  CHECK(result.totalSamples == exact.size());

  auto counterclockwise = circleTrace(kPerimeterRadius, false);
  CHECK(analyzePerimeter(counterclockwise.data(), counterclockwise.size()).complete);

  auto wandering = exact;
  for (size_t i = 0; i < wandering.size(); ++i) {
    const double radius = i % 2 ? 90.0 : 225.0;
    const double dx = wandering[i].x - kCenter.x;
    const double dy = wandering[i].y - kCenter.y;
    const double scale = radius / std::hypot(dx, dy);
    wandering[i] = {kCenter.x + dx * scale, kCenter.y + dy * scale};
  }
  CHECK(analyzePerimeter(wandering.data(), wandering.size()).complete);

  auto missingSector = exact;
  constexpr double pi = 3.14159265358979323846;
  for (auto& point : missingSector) {
    double observedAngle = std::atan2(point.x - kCenter.x, -(point.y - kCenter.y));
    if (observedAngle < 0) observedAngle += 2.0 * pi;
    const auto sector = static_cast<size_t>(std::floor(
        (observedAngle + pi / kPerimeterSectorCount) /
        (2.0 * pi / kPerimeterSectorCount))) % kPerimeterSectorCount;
    if (sector == 5) {
      const double angle = 98.0 * pi / 180.0;
      point = {kCenter.x + std::sin(angle) * kPerimeterRadius,
               kCenter.y - std::cos(angle) * kPerimeterRadius};
    }
  }
  result = analyzePerimeter(missingSector.data(), missingSector.size());
  CHECK(!result.complete);
  CHECK(result.sectorsCovered == 15);

  auto shortSweep = circleTrace(kPerimeterRadius);
  for (size_t i = 0; i < shortSweep.size(); ++i) {
    const double angle = 1.5 * pi * i / (shortSweep.size() - 1);
    shortSweep[i] = {kCenter.x + std::sin(angle) * kPerimeterRadius,
                     kCenter.y - std::cos(angle) * kPerimeterRadius};
  }
  CHECK(!analyzePerimeter(shortSweep.data(), shortSweep.size()).complete);

  CHECK(!analyzePerimeter(exact.data(), 16).complete);
  CHECK(!analyzePerimeter(nullptr, exact.size()).complete);
  auto invalid = exact;
  invalid[4].x = std::numeric_limits<double>::quiet_NaN();
  result = analyzePerimeter(invalid.data(), invalid.size());
  CHECK(!result.complete);
  CHECK(result.totalSamples == exact.size() - 1);
}

void checkWarp() {
  using namespace touchcal;
  constexpr double pi = 3.14159265358979323846;
  const Affine identity{1, 0, 0, 0, 1, 0};
  auto map = zeroWarp(identity);
  CHECK(validWarp(map));
  checkPoint(applyWarp(map, {31, 402}), {31, 402});

  // A ten-percent radial correction behaves like local zoom, not a fixed shift.
  for (size_t ring = 0; ring < kRadialRingCount; ++ring) {
    for (size_t sector = 0; sector < kPerimeterSectorCount; ++sector) {
      const double angle = 2.0 * pi * sector / kPerimeterSectorCount;
      map.rings[ring][sector] = {std::sin(angle) * kRadialRings[ring] * 0.1,
                                 -std::cos(angle) * kRadialRings[ring] * 0.1};
    }
  }
  CHECK(validWarp(map));
  checkPoint(applyWarp(map, {334, 233}), {344, 233}, 1e-6);
  checkPoint(applyWarp(map, {134, 233}), {124, 233}, 1e-6);
  checkPoint(applyWarp(map, {234, 133}), {234, 123}, 1e-6);

  // The same radius can receive a different correction in a different region.
  map = zeroWarp(identity);
  map.rings[2][4] = {18, 0};
  map.rings[2][12] = {-8, 0};
  CHECK(validWarp(map));
  checkPoint(applyWarp(map, {404, 233}), {422, 233});
  checkPoint(applyWarp(map, {64, 233}), {56, 233});

  // Angular interpolation is continuous across the sector 15/0 seam.
  map = zeroWarp(identity);
  map.rings[1][15] = {10, 0};
  map.rings[1][0] = {20, 0};
  const double seamAngle = 15.5 * 2.0 * pi / kPerimeterSectorCount;
  Point seam{kCenter.x + std::sin(seamAngle) * 120,
             kCenter.y - std::cos(seamAngle) * 120};
  Point seamMapped = applyWarp(map, seam);
  CHECK(close(seamMapped.x, seam.x + 15, 1e-6));
  CHECK(close(seamMapped.y, seam.y, 1e-6));

  // The center blends smoothly into the first radial ring.
  map = zeroWarp(identity);
  map.center = {2, -4};
  for (auto& value : map.rings[0]) value = {12, 6};
  checkPoint(applyWarp(map, kCenter), {236, 229});
  checkPoint(applyWarp(map, {234, 198}), {241, 199}, 1e-6);

  map = zeroWarp(identity);
  map.center = {48, 0};
  for (auto& ring : map.rings) for (auto& value : ring) value = {48, 0};
  CHECK(validWarp(map));
  map.center.dx = 48.001;
  CHECK(!validWarp(map));
  map = zeroWarp(identity);
  map.rings[1][3] = {33, 0};
  CHECK(!validWarp(map));
  map = zeroWarp(identity);
  map.rings[0][0].dy = std::numeric_limits<double>::quiet_NaN();
  CHECK(!validWarp(map));

  Residual impulse[kPerimeterSectorCount]{};
  Residual smoothed[kPerimeterSectorCount]{};
  impulse[0] = {8, 4};
  smoothRing(impulse, smoothed);
  CHECK(close(smoothed[0].dx, 4));
  CHECK(close(smoothed[0].dy, 2));
  CHECK(close(smoothed[1].dx, 2));
  CHECK(close(smoothed[15].dx, 2));
  CHECK(close(smoothed[8].dx, 0));
  smoothRing(impulse, impulse);
  CHECK(close(impulse[0].dx, 4));
  CHECK(close(impulse[15].dx, 2));

  Residual clockwise[kPerimeterSectorCount]{};
  Residual counterclockwise[kPerimeterSectorCount]{};
  Residual combined[kPerimeterSectorCount]{};
  clockwise[3] = {12, -8};
  counterclockwise[3] = {4, 6};
  averageRings(clockwise, counterclockwise, combined);
  CHECK(close(combined[3].dx, 8));
  CHECK(close(combined[3].dy, -1));
  CHECK(close(combined[4].dx, 0));
  averageRings(clockwise, counterclockwise, clockwise);
  CHECK(close(clockwise[3].dx, 8));
  CHECK(close(clockwise[3].dy, -1));
}

void checkRecord() {
  using namespace touchcal;
  const Affine transform{0.4, 0.02, 8, -0.01, 0.42, 9};
  const Record record = makeRecord(transform);
  CHECK(record.magic == kRecordMagic);
  CHECK(record.version == kRecordVersion);
  CHECK(record.width == kDisplayWidth);
  CHECK(record.height == kDisplayHeight);
  CHECK(record.rotation == kRotation);
  CHECK(record.reserved == 0);
  CHECK(record.checksum == expectedChecksum(record));
  CHECK(checksumRecord(record) == expectedChecksum(record));
  CHECK(validateRecord(record));

  auto tampered = record;
  ++tampered.width;
  CHECK(!validateRecord(tampered));
  tampered = record;
  ++tampered.version;
  CHECK(!validateRecord(tampered));
  tampered = record;
  tampered.rotation = 1;
  CHECK(!validateRecord(tampered));
  tampered = record;
  tampered.reserved = 1;
  tampered.checksum = checksumRecord(tampered);
  CHECK(!validateRecord(tampered));
  tampered = record;
  tampered.coefficients[0] = std::numeric_limits<float>::quiet_NaN();
  tampered.checksum = checksumRecord(tampered);
  CHECK(!validateRecord(tampered));
  tampered = record;
  std::fill(std::begin(tampered.coefficients), std::end(tampered.coefficients), 0.0f);
  tampered.checksum = checksumRecord(tampered);
  CHECK(!validateRecord(tampered));
  tampered = record;
  tampered.checksum ^= 1;
  CHECK(!validateRecord(tampered));
  tampered = record;
  ++tampered.magic;
  tampered.checksum = checksumRecord(tampered);
  CHECK(!validateRecord(tampered));
  CHECK(!validateRecord(makeRecord({0, 0, 0, 0, 0, 0})));

  WarpMap map = zeroWarp(transform);
  map.center = {2, -3};
  for (size_t ring = 0; ring < kRadialRingCount; ++ring) {
    for (size_t sector = 0; sector < kPerimeterSectorCount; ++sector) {
      map.rings[ring][sector] = {double(ring + 1), double(sector % 3) - 1};
    }
  }
  const RecordV2 recordV2 = makeRecordV2(map, 7);
  CHECK(recordV2.magic == kRecordMagic);
  CHECK(recordV2.version == kRecordVersionV2);
  CHECK(recordV2.width == kDisplayWidth);
  CHECK(recordV2.height == kDisplayHeight);
  CHECK(recordV2.rotation == kRotation);
  CHECK(recordV2.angularSectors == kPerimeterSectorCount);
  CHECK(recordV2.radialRings == kRadialRingCount);
  CHECK(recordV2.reserved == 0);
  CHECK(recordV2.generation == 7);
  CHECK(recordV2.checksum == expectedChecksumV2(recordV2));
  CHECK(checksumRecordV2(recordV2) == expectedChecksumV2(recordV2));
  CHECK(validateRecordV2(recordV2));

  auto changedV2 = recordV2;
  ++changedV2.generation;
  CHECK(!validateRecordV2(changedV2));
  changedV2.checksum = checksumRecordV2(changedV2);
  CHECK(validateRecordV2(changedV2));
  changedV2 = recordV2;
  ++changedV2.angularSectors;
  changedV2.checksum = checksumRecordV2(changedV2);
  CHECK(!validateRecordV2(changedV2));
  changedV2 = recordV2;
  changedV2.residuals[1][4][0] = 100;
  changedV2.checksum = checksumRecordV2(changedV2);
  CHECK(!validateRecordV2(changedV2));
  changedV2 = recordV2;
  changedV2.checksum ^= 1;
  CHECK(!validateRecordV2(changedV2));

  const WarpMap migrated = warpFromV1(record);
  CHECK(validWarp(migrated));
  checkPoint(applyWarp(migrated, {333, 222}), apply(transform, {333, 222}), 1e-4);
  const WarpMap rejectedMigration = warpFromV1(tampered);
  CHECK(!validWarp(rejectedMigration));
}

}  // namespace

int main() {
  checkConstants();
  checkSamples();
  checkAsteroidTargets();
  checkFits();
  checkGeneratedFits();
  checkVerification();
  checkPerimeterCollection();
  checkWarp();
  checkRecord();
  if (failures) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return 1;
  }
  std::cout << "Touch calibration contract: " << assertions << " assertions passed.\n";
  return 0;
}
