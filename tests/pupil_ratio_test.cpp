// Simple test to verify the pupil-to-limbus ratio model compiles and works
// without SDL dependency
#include "eye.h"
#include <cmath>
#include <cstdio>

using namespace uncanny;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        printf("FAIL: %s\n", what);
        ++failures;
    } else {
        printf("PASS: %s\n", what);
    }
}

int main() {
    // Test 1: Minimum dilation yields Rmin
    {
        EyeCalibration cal{100.0F, 0.12F, 0.42F, 100.0F, 100.0F};
        float ratio = dilationToPupilRatio(0.0F, cal);
        check(std::fabs(ratio - cal.pupilRatioMin) < 1e-6,
              "D=0.0 should yield pupilRatio == Rmin (0.12)");
        printf("  ratio = %.6f, expected = %.6f\n", ratio, cal.pupilRatioMin);
    }

    // Test 2: Midpoint dilation yields midpoint ratio
    {
        EyeCalibration cal{100.0F, 0.12F, 0.42F, 100.0F, 100.0F};
        float ratio = dilationToPupilRatio(0.5F, cal);
        float expected = 0.12F + 0.5F * (0.42F - 0.12F);
        check(std::fabs(ratio - expected) < 1e-6,
              "D=0.5 should yield midpoint pupilRatio (0.27)");
        printf("  ratio = %.6f, expected = %.6f\n", ratio, expected);
    }

    // Test 3: Maximum dilation yields Rmax
    {
        EyeCalibration cal{100.0F, 0.12F, 0.42F, 100.0F, 100.0F};
        float ratio = dilationToPupilRatio(1.0F, cal);
        check(std::fabs(ratio - cal.pupilRatioMax) < 1e-6,
              "D=1.0 should yield pupilRatio == Rmax (0.42)");
        printf("  ratio = %.6f, expected = %.6f\n", ratio, cal.pupilRatioMax);
    }

    // Test 4: Clamp behavior
    {
        EyeCalibration cal{100.0F, 0.12F, 0.42F, 100.0F, 100.0F};
        float belowMin = dilationToPupilRatio(-0.5F, cal);
        check(std::fabs(belowMin - cal.pupilRatioMin) < 1e-6,
              "D < 0 should clamp to Rmin");
        float aboveMax = dilationToPupilRatio(1.5F, cal);
        check(std::fabs(aboveMax - cal.pupilRatioMax) < 1e-6,
              "D > 1 should clamp to Rmax");
    }

    // Test 5: Cross-eye proportionality
    {
        EyeCalibration eyeA{100.0F, 0.12F, 0.42F, 100.0F, 100.0F};
        EyeCalibration eyeB{130.0F, 0.12F, 0.42F, 130.0F, 130.0F};

        for (float d = 0.0F; d <= 1.0F; d += 0.1F) {
            float ratioA = dilationToPupilRatio(d, eyeA);
            float ratioB = dilationToPupilRatio(d, eyeB);
            check(std::fabs(ratioA - ratioB) < 1e-6,
                  "pupilRatio should be identical across eyes at same Rmin/Rmax");

            float pupilDiameterA = eyeA.limbusDiameter * ratioA;
            float pupilDiameterB = eyeB.limbusDiameter * ratioB;
            float propA = pupilDiameterA / eyeA.limbusDiameter;
            float propB = pupilDiameterB / eyeB.limbusDiameter;
            check(std::fabs(propA - propB) < 1e-6,
                  "PupilA/L_A should equal PupilB/L_B");
        }
    }

    // Test 6: Center invariance
    {
        EyeCalibration cal{100.0F, 0.12F, 0.42F, 100.0F, 100.0F};
        check(std::fabs(cal.pupilCenterX - 100.0F) < 1e-6,
              "pupilCenterX should be 100.0");
        check(std::fabs(cal.pupilCenterY - 100.0F) < 1e-6,
              "pupilCenterY should be 100.0");
    }

    // Test 7: Boundary invariance
    {
        EyeCalibration cal{100.0F, 0.12F, 0.42F, 100.0F, 100.0F};
        for (float d = 0.0F; d <= 1.0F; d += 0.05F) {
            float ratio = dilationToPupilRatio(d, cal);
            check(ratio >= cal.pupilRatioMin - 1e-6,
                  "pupilRatio should not go below Rmin");
            check(ratio <= cal.pupilRatioMax + 1e-6,
                  "pupilRatio should not exceed Rmax");
        }
    }

    // Test 8: pupilGeometry with calibration
    {
        EyeCalibration cal{100.0F, 0.12F, 0.42F, 100.0F, 100.0F};
        const int irisHeight = 128;
        double irisRadius = static_cast<double>(irisHeight) / 2.0;

        // irisScale=1023 -> D=0 -> Rmin (smaller pupil)
        PupilGeometry minGeo = pupilGeometry(1023, 0, 0.0F, irisHeight, cal);
        // irisScale=0 -> D=1 -> Rmax (larger pupil)
        PupilGeometry maxGeo = pupilGeometry(0, 0, 0.0F, irisHeight, cal);

        check(minGeo.radius <= maxGeo.radius + 1e-9,
              "pupil radius at min dilation should be smaller than max dilation");
        check(minGeo.radius <= irisRadius + 1e-9,
              "pupil radius at min dilation should be within iris disc");
        check(maxGeo.radius <= irisRadius + 1e-9,
              "pupil radius at max dilation should be within iris disc");

        printf("  minGeo.radius = %.6f, maxGeo.radius = %.6f, irisRadius = %.1f\n",
               minGeo.radius, maxGeo.radius, irisRadius);
    }

    if (failures == 0) {
        printf("\nAll tests passed!\n");
        return 0;
    } else {
        printf("\n%d test(s) failed!\n", failures);
        return 1;
    }
}
