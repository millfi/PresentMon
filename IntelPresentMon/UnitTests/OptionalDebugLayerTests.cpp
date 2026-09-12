#include <Core/source/gfx/OptionalDebugLayer.h>
#include <CppUnitTest.h>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GraphicsTests
{
    TEST_CLASS(OptionalDebugLayerTests)
    {
    public:
        TEST_METHOD(MissingDebugLayerRetriesWithoutDebugging)
        {
            bool debug = true;
            std::vector<bool> attempts;
            const auto result = p2c::gfx::CreateWithOptionalDebugLayer(debug, [&](bool enabled) {
                attempts.push_back(enabled);
                return enabled ? DXGI_ERROR_SDK_COMPONENT_MISSING : S_OK;
            });
            Assert::AreEqual(S_OK, result);
            Assert::IsFalse(debug);
            Assert::IsTrue(attempts == std::vector<bool>{ true, false });
        }

        TEST_METHOD(AvailableDebugLayerRemainsEnabled)
        {
            bool debug = true;
            int attempts = 0;
            const auto result = p2c::gfx::CreateWithOptionalDebugLayer(debug, [&](bool enabled) {
                ++attempts;
                Assert::IsTrue(enabled);
                return S_OK;
            });
            Assert::AreEqual(S_OK, result);
            Assert::IsTrue(debug);
            Assert::AreEqual(1, attempts);
        }

        TEST_METHOD(OtherErrorsAreNotRetried)
        {
            bool debug = true;
            int attempts = 0;
            const auto result = p2c::gfx::CreateWithOptionalDebugLayer(debug, [&](bool) {
                ++attempts;
                return E_OUTOFMEMORY;
            });
            Assert::AreEqual(E_OUTOFMEMORY, result);
            Assert::IsTrue(debug);
            Assert::AreEqual(1, attempts);
        }

        TEST_METHOD(ReleaseFailureIsNotRetried)
        {
            bool debug = false;
            int attempts = 0;
            const auto result = p2c::gfx::CreateWithOptionalDebugLayer(debug, [&](bool enabled) {
                ++attempts;
                Assert::IsFalse(enabled);
                return DXGI_ERROR_SDK_COMPONENT_MISSING;
            });
            Assert::AreEqual(DXGI_ERROR_SDK_COMPONENT_MISSING, result);
            Assert::IsFalse(debug);
            Assert::AreEqual(1, attempts);
        }

        TEST_METHOD(FallbackFailureIsPreservedAndDisablesFurtherDebugRequests)
        {
            bool debug = true;
            const auto result = p2c::gfx::CreateWithOptionalDebugLayer(debug, [](bool enabled) {
                return enabled ? DXGI_ERROR_SDK_COMPONENT_MISSING : E_FAIL;
            });
            Assert::AreEqual(E_FAIL, result);
            Assert::IsFalse(debug);
            int attempts = 0;
            const auto factoryResult = p2c::gfx::CreateWithOptionalDebugLayer(debug, [&](bool enabled) {
                ++attempts;
                Assert::IsFalse(enabled);
                return S_OK;
            });
            Assert::AreEqual(S_OK, factoryResult);
            Assert::AreEqual(1, attempts);
        }
    };
}
