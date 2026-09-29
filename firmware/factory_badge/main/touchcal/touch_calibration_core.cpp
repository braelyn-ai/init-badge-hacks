#include "touch_calibration_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace touchcal {

const Point kCardinalTargets[kCardinalCount] = {
  {234, 63}, {234, 403}, {64, 233}, {404, 233},
};
const double kRadialRings[kRadialRingCount] = {70, 120, 170};

namespace {

bool finitePoint(Point point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const size_t middle = values.size() / 2;
  return values.size() & 1 ? values[middle] : (values[middle - 1] + values[middle]) * 0.5;
}

uint32_t crcByte(uint32_t crc, uint8_t value) {
  crc ^= value;
  for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  return crc;
}

void crcScalar(uint32_t& crc, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) crc = crcByte(crc, static_cast<uint8_t>(value >> (8 * i)));
}

}  // namespace

SampleResult reduceSamples(const Point* samples, size_t count, uint32_t durationMs) {
  if (!samples || count < kMinimumSamples || durationMs < kMinimumSampleDurationMs) return {false, {0, 0}};
  std::vector<double> xs;
  std::vector<double> ys;
  xs.reserve(count);
  ys.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    if (!finitePoint(samples[i])) return {false, {0, 0}};
    xs.push_back(samples[i].x);
    ys.push_back(samples[i].y);
  }
  const auto [xmin, xmax] = std::minmax_element(xs.begin(), xs.end());
  const auto [ymin, ymax] = std::minmax_element(ys.begin(), ys.end());
  if (*xmax - *xmin > kMaximumSampleSpan || *ymax - *ymin > kMaximumSampleSpan) return {false, {0, 0}};
  return {true, {median(xs), median(ys)}};
}

SampleResult reduceAttempt(const Point* samples,size_t count){if(!samples||!count)return{false,{0,0}};std::vector<double>xs,ys;for(size_t i=0;i<count;++i)if(finitePoint(samples[i])){xs.push_back(samples[i].x);ys.push_back(samples[i].y);}if(xs.empty())return{false,{0,0}};return{true,{median(xs),median(ys)}};}
bool asteroidTarget(size_t attempt,AsteroidTarget*target){if(!target||attempt>=kAsteroidAttemptCount)return false;constexpr double pi=3.14159265358979323846;constexpr uint8_t radii[]={10,14,18,22};constexpr uint32_t colors[]={0xff7a00,0x00d9ff,0x9dff00,0xff3bd4,0xffffff};size_t id=(attempt*20+7)%kAsteroidAttemptCount;Point center=kCenter;uint8_t layer=0,sector=255;if(id>=kCenterAttemptCount){layer=id<19?1:id<35?2:3;sector=static_cast<uint8_t>(id<19?id-3:id<35?id-19:id-35);double radius=kRadialRings[layer-1],angle=sector*2.*pi/kPerimeterSectorCount;center={kCenter.x+std::sin(angle)*radius,kCenter.y-std::cos(angle)*radius};}*target={center,static_cast<TargetShape>(attempt%4),radii[(attempt*3+1)%4],colors[(attempt*2+3)%5],layer,sector};return true;}

bool fitAffine(const Point* raw, const Point* screen, size_t count, Affine* result) {
  if (!raw || !screen || !result || count < 4) return false;
  double meanRawX = 0;
  double meanRawY = 0;
  double meanScreenX = 0;
  double meanScreenY = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!finitePoint(raw[i]) || !finitePoint(screen[i])) return false;
    meanRawX += raw[i].x;
    meanRawY += raw[i].y;
    meanScreenX += screen[i].x;
    meanScreenY += screen[i].y;
  }
  meanRawX /= count;
  meanRawY /= count;
  meanScreenX /= count;
  meanScreenY /= count;

  double xx = 0, xy = 0, yy = 0;
  double xToScreenX = 0, yToScreenX = 0;
  double xToScreenY = 0, yToScreenY = 0;
  for (size_t i = 0; i < count; ++i) {
    const double dx = raw[i].x - meanRawX;
    const double dy = raw[i].y - meanRawY;
    const double dsx = screen[i].x - meanScreenX;
    const double dsy = screen[i].y - meanScreenY;
    xx += dx * dx;
    xy += dx * dy;
    yy += dy * dy;
    xToScreenX += dx * dsx;
    yToScreenX += dy * dsx;
    xToScreenY += dx * dsy;
    yToScreenY += dy * dsy;
  }
  const double determinant = xx * yy - xy * xy;
  const double scale = std::max(xx, yy);
  if (!(scale > 0) || std::abs(determinant) <= scale * scale * 1e-12) return false;

  const double a = (xToScreenX * yy - yToScreenX * xy) / determinant;
  const double b = (yToScreenX * xx - xToScreenX * xy) / determinant;
  const double d = (xToScreenY * yy - yToScreenY * xy) / determinant;
  const double e = (yToScreenY * xx - xToScreenY * xy) / determinant;
  const double c = meanScreenX - a * meanRawX - b * meanRawY;
  const double f = meanScreenY - d * meanRawX - e * meanRawY;
  *result = {a, b, c, d, e, f};
  return validAffine(*result);
}

Point apply(const Affine& transform, Point raw) {
  return {transform.a * raw.x + transform.b * raw.y + transform.c,
          transform.d * raw.x + transform.e * raw.y + transform.f};
}

bool validAffine(const Affine& transform) {
  const double values[] = {transform.a, transform.b, transform.c,
                           transform.d, transform.e, transform.f};
  for (double value : values) if (!std::isfinite(value)) return false;
  return std::abs(transform.a * transform.e - transform.b * transform.d) >= 1e-9;
}

WarpMap zeroWarp(const Affine& transform) {
  WarpMap map{};
  map.affine = transform;
  return map;
}

Point applyWarp(const WarpMap& map, Point raw) {
  constexpr double pi = 3.14159265358979323846;
  const Point base = apply(map.affine, raw);
  const double x = base.x - kCenter.x, y = base.y - kCenter.y;
  const double radius = std::hypot(x, y);
  double angle = std::atan2(x, -y); if (angle < 0) angle += 2.0 * pi;
  const double angular = angle * kPerimeterSectorCount / (2.0 * pi);
  const size_t lo = static_cast<size_t>(std::floor(angular)) % kPerimeterSectorCount;
  const size_t hi = (lo + 1) % kPerimeterSectorCount; const double t = angular - std::floor(angular);
  auto atRing = [&](size_t ring) { return Residual{map.rings[ring][lo].dx * (1-t) + map.rings[ring][hi].dx * t,
                                                   map.rings[ring][lo].dy * (1-t) + map.rings[ring][hi].dy * t}; };
  Residual correction{};
  if (radius <= kRadialRings[0]) { auto o=atRing(0); double u=radius/kRadialRings[0]; correction={map.center.dx*(1-u)+o.dx*u,map.center.dy*(1-u)+o.dy*u}; }
  else if (radius <= kRadialRings[1]) { auto i=atRing(0),o=atRing(1); double u=(radius-kRadialRings[0])/(kRadialRings[1]-kRadialRings[0]); correction={i.dx*(1-u)+o.dx*u,i.dy*(1-u)+o.dy*u}; }
  else if (radius <= kRadialRings[2]) { auto i=atRing(1),o=atRing(2); double u=(radius-kRadialRings[1])/(kRadialRings[2]-kRadialRings[1]); correction={i.dx*(1-u)+o.dx*u,i.dy*(1-u)+o.dy*u}; }
  else correction=atRing(2);
  return {base.x+correction.dx,base.y+correction.dy};
}

bool validWarp(const WarpMap& map) {
  if (!validAffine(map.affine)) return false;
  auto valid=[](Residual v){return std::isfinite(v.dx)&&std::isfinite(v.dy)&&std::hypot(v.dx,v.dy)<=kMaximumResidualMagnitude;};
  auto near=[](Residual a,Residual b){return std::hypot(a.dx-b.dx,a.dy-b.dy)<=kMaximumNeighborDelta;};
  if(!valid(map.center)) return false;
  for(size_t r=0;r<kRadialRingCount;++r) for(size_t s=0;s<kPerimeterSectorCount;++s){auto v=map.rings[r][s];if(!valid(v)||!near(v,map.rings[r][(s+1)%kPerimeterSectorCount])||(r?!near(v,map.rings[r-1][s]):!near(v,map.center)))return false;}
  return true;
}

void smoothRing(const Residual input[16], Residual output[16]) {
  if(!input||!output) return;
  Residual copy[kPerimeterSectorCount];for(size_t i=0;i<kPerimeterSectorCount;++i)copy[i]=input[i];for(size_t i=0;i<kPerimeterSectorCount;++i){auto p=copy[(i+kPerimeterSectorCount-1)%kPerimeterSectorCount],n=copy[(i+1)%kPerimeterSectorCount];output[i]={p.dx*.25+copy[i].dx*.5+n.dx*.25,p.dy*.25+copy[i].dy*.5+n.dy*.25};}
}

void averageRings(const Residual clockwise[16],const Residual counterclockwise[16],Residual output[16]){if(!clockwise||!counterclockwise||!output)return;Residual a[kPerimeterSectorCount],b[kPerimeterSectorCount];for(size_t i=0;i<kPerimeterSectorCount;++i){a[i]=clockwise[i];b[i]=counterclockwise[i];}for(size_t i=0;i<kPerimeterSectorCount;++i)output[i]={(a[i].dx+b[i].dx)*.5,(a[i].dy+b[i].dy)*.5};}

Verification verify(const Point* expected, const Point* observed, size_t count) {
  if (!expected || !observed || !count) return {0, 0, false};
  double total = 0;
  double maximum = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!finitePoint(expected[i]) || !finitePoint(observed[i])) return {0, 0, false};
    const double error = std::hypot(expected[i].x - observed[i].x,
                                    expected[i].y - observed[i].y);
    total += error;
    maximum = std::max(maximum, error);
  }
  const double mean = total / count;
  return {mean, maximum, mean <= kMaximumMeanError && maximum <= kMaximumPointError};
}

size_t perimeterSector(Point point) {
  if (!finitePoint(point)) return kPerimeterSectorCount;
  constexpr double pi = 3.14159265358979323846;
  constexpr double sectorWidth = 2.0 * pi / kPerimeterSectorCount;
  double angle = std::atan2(point.x - kCenter.x, -(point.y - kCenter.y));
  if (angle < 0) angle += 2.0 * pi;
  return static_cast<size_t>(std::floor((angle + sectorWidth * 0.5) / sectorWidth)) %
         kPerimeterSectorCount;
}

PerimeterCoverage analyzePerimeter(const Point* observed, size_t count) {
  if (!observed) return {0, 0, false};
  size_t counts[kPerimeterSectorCount] = {};
  size_t total = 0;
  for (size_t i = 0; i < count; ++i) {
    const size_t sector = perimeterSector(observed[i]);
    if (sector < kPerimeterSectorCount) { ++counts[sector]; ++total; }
  }
  size_t covered = 0;
  for (size_t value : counts) covered += value >= kSamplesPerPerimeterSector;
  return {covered, total, covered == kPerimeterSectorCount};
}

uint32_t checksumRecord(const Record& record) {
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

Record makeRecord(const Affine& transform) {
  Record record{kRecordMagic, kRecordVersion, kDisplayWidth, kDisplayHeight,
                kRotation, 0,
                {static_cast<float>(transform.a), static_cast<float>(transform.b),
                 static_cast<float>(transform.c), static_cast<float>(transform.d),
                 static_cast<float>(transform.e), static_cast<float>(transform.f)}, 0};
  record.checksum = checksumRecord(record);
  return record;
}

bool validateRecord(const Record& record) {
  if (sizeof(Record) != 40 || record.magic != kRecordMagic ||
      record.version != kRecordVersion || record.width != kDisplayWidth ||
      record.height != kDisplayHeight || record.rotation != kRotation ||
      record.reserved != 0 || record.checksum != checksumRecord(record)) return false;
  return validAffine({record.coefficients[0], record.coefficients[1], record.coefficients[2],
                      record.coefficients[3], record.coefficients[4], record.coefficients[5]});
}

uint32_t checksumRecordV2(const RecordV2& record) {
  uint32_t crc=0xffffffffu;crcScalar(crc,record.magic,4);crcScalar(crc,record.version,2);crcScalar(crc,record.width,2);crcScalar(crc,record.height,2);crcScalar(crc,record.rotation,1);crcScalar(crc,record.angularSectors,1);crcScalar(crc,record.radialRings,1);crcScalar(crc,record.reserved,1);crcScalar(crc,record.generation,4);
  auto feed=[&](float v){uint32_t bits;std::memcpy(&bits,&v,4);crcScalar(crc,bits,4);};for(float v:record.affine)feed(v);for(float v:record.centerResidual)feed(v);for(const auto&r:record.residuals)for(const auto&s:r)for(float v:s)feed(v);return crc^0xffffffffu;
}
RecordV2 makeRecordV2(const WarpMap& map,uint32_t generation){RecordV2 r{};r.magic=kRecordMagic;r.version=kRecordVersionV2;r.width=kDisplayWidth;r.height=kDisplayHeight;r.rotation=kRotation;r.angularSectors=kPerimeterSectorCount;r.radialRings=kRadialRingCount;r.generation=generation;double a[]={map.affine.a,map.affine.b,map.affine.c,map.affine.d,map.affine.e,map.affine.f};for(size_t i=0;i<6;++i)r.affine[i]=a[i];r.centerResidual[0]=map.center.dx;r.centerResidual[1]=map.center.dy;for(size_t i=0;i<kRadialRingCount;++i)for(size_t s=0;s<kPerimeterSectorCount;++s){r.residuals[i][s][0]=map.rings[i][s].dx;r.residuals[i][s][1]=map.rings[i][s].dy;}r.checksum=checksumRecordV2(r);return r;}
bool validateRecordV2(const RecordV2&r){if(sizeof(RecordV2)!=440||r.magic!=kRecordMagic||r.version!=kRecordVersionV2||r.width!=kDisplayWidth||r.height!=kDisplayHeight||r.rotation!=kRotation||r.angularSectors!=kPerimeterSectorCount||r.radialRings!=kRadialRingCount||r.reserved||r.checksum!=checksumRecordV2(r))return false;WarpMap m{};m.affine={r.affine[0],r.affine[1],r.affine[2],r.affine[3],r.affine[4],r.affine[5]};m.center={r.centerResidual[0],r.centerResidual[1]};for(size_t i=0;i<kRadialRingCount;++i)for(size_t s=0;s<kPerimeterSectorCount;++s)m.rings[i][s]={r.residuals[i][s][0],r.residuals[i][s][1]};return validWarp(m);}
WarpMap warpFromV1(const Record&r){if(!validateRecord(r))return{};return zeroWarp({r.coefficients[0],r.coefficients[1],r.coefficients[2],r.coefficients[3],r.coefficients[4],r.coefficients[5]});}

}  // namespace touchcal
