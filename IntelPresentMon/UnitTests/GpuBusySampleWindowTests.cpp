// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include <KernelProcess/GpuBusySampleWindow.h>
#include <CppUnitTest.h>
#include <cmath>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace AutomaticTargetingTests
{
    TEST_CLASS(GpuBusySampleWindowTests)
    {
    public:
        TEST_METHOD(RequiresTenSamplesEvenWhenValuesAlreadyDiffer)
        {
            kproc::GpuBusySampleWindow window;
            Assert::IsFalse(window.HasVariation());
            for (int i = 0; i < 9; ++i) {
                window.Add((double)i);
                Assert::IsFalse(window.HasVariation());
            }
            window.Add(9.);
            Assert::IsTrue(window.HasVariation());
        }

        TEST_METHOD(RejectsConstantZeroAndConstantNonzeroValues)
        {
            for (const auto constant : { 0., 1.25 }) {
                kproc::GpuBusySampleWindow window;
                for (int i = 0; i < 30; ++i) {
                    window.Add(constant);
                    Assert::IsFalse(window.HasVariation());
                }
            }
        }

        TEST_METHOD(AcceptsOneDifferentSampleAtAnyPosition)
        {
            for (int changedIndex = 0; changedIndex < 10; ++changedIndex) {
                kproc::GpuBusySampleWindow window;
                for (int i = 0; i < 10; ++i) window.Add(i == changedIndex ? 0.001 : 0.);
                Assert::IsTrue(window.HasVariation());
            }
        }

        TEST_METHOD(OldVariationExpiresFromTheLatestTenSamples)
        {
            kproc::GpuBusySampleWindow window;
            for (int i = 0; i < 9; ++i) window.Add(0.);
            window.Add(1.);
            Assert::IsTrue(window.HasVariation());
            for (int i = 0; i < 9; ++i) {
                window.Add(0.);
                Assert::IsTrue(window.HasVariation());
            }
            window.Add(0.);
            Assert::IsFalse(window.HasVariation());
        }

        TEST_METHOD(InvalidSamplesCannotCountAsVariation)
        {
            for (const auto invalid : { std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() }) {
                kproc::GpuBusySampleWindow window;
                for (int i = 0; i < 10; ++i) window.Add((double)i);
                window.Add(invalid);
                Assert::IsFalse(window.HasVariation());
                for (int i = 0; i < 9; ++i) {
                    window.Add((double)i);
                    Assert::IsFalse(window.HasVariation());
                }
                window.Add(9.);
                Assert::IsTrue(window.HasVariation());
            }
        }

        TEST_METHOD(ComparesRawPrecisionAndResetRequiresNewSamples)
        {
            kproc::GpuBusySampleWindow window;
            for (int i = 0; i < 9; ++i) window.Add(1.);
            window.Add(std::nextafter(1., 2.));
            Assert::IsTrue(window.HasVariation(), L"Do not round raw values or impose a minimum change");
            window.Reset();
            for (int i = 0; i < 9; ++i) {
                window.Add((double)i);
                Assert::IsFalse(window.HasVariation());
            }
            window.Add(9.);
            Assert::IsTrue(window.HasVariation());
        }
    };
}
