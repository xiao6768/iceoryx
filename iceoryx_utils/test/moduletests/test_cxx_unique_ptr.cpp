// Copyright (c) 2019 by Robert Bosch GmbH. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "iceoryx_utils/cxx/unique_ptr.hpp"
#include "test.hpp"

#include <iostream>

using namespace ::testing;

static constexpr double_t X_POS = 0.0;
static constexpr double_t Y_POS = 1.1;
static constexpr double_t Z_POS = 2.2;

struct Position
{
    double_t x{X_POS};
    double_t y{Y_POS};
    double_t z{Z_POS};
};

class UniquePtrTest : public Test
{
  public:
    UniquePtrTest()
    {
    }

    void SetUp()
    {
        m_deleterCalled = false;
        m_anotherDeleterCalled = false;
    }

    void TearDown()
    {
    }

    bool m_deleterCalled;
    bool m_anotherDeleterCalled;

    std::function<void(Position* const)> deleter = [this](Position* const p) {
        m_deleterCalled = true;
        delete p;
    };

    std::function<void(Position* const)> anotherDeleter = [this](Position* const p) {
        m_anotherDeleterCalled = true;
        delete p;
    };
};

TEST_F(UniquePtrTest, CtorWithNullptrSetsPtrToNull)
{
    {
        auto sut = iox::cxx::unique_ptr<Position>(nullptr);
        EXPECT_FALSE(sut);
        EXPECT_EQ(sut.get(), nullptr);
    }

    EXPECT_FALSE(m_deleterCalled);
}

TEST_F(UniquePtrTest, CtorWithOnlyDeleterSetsPtrToNullAndDoesntCallDeleter)
{
    {
        auto sut = iox::cxx::unique_ptr<Position>(deleter);
        EXPECT_FALSE(sut);
        EXPECT_EQ(sut.get(), nullptr);
    }

    EXPECT_FALSE(m_deleterCalled);
}

TEST_F(UniquePtrTest, CtorWithObjectPtrAndDeleterSetsPtrToObjectAndCallsDeleter)
{
    {
        auto object = new Position();
        auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
        EXPECT_TRUE(sut);
        EXPECT_EQ(sut.get(), object);
    }

    EXPECT_TRUE(m_deleterCalled);
}

TEST_F(UniquePtrTest, CtorUsingMoveWithObjectPtrAndDeleterSetsPtrToObjectAndCallsDeleter)
{
    {
        auto object = new Position();
        auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
        auto anotherSut = iox::cxx::unique_ptr<Position>(std::move(sut));

        EXPECT_FALSE(m_deleterCalled);
        EXPECT_FALSE(sut);
        EXPECT_EQ(anotherSut.get(), object);
    }

    EXPECT_TRUE(m_deleterCalled);
}

TEST_F(UniquePtrTest, MoveAssignmentUniquePtrs)
{
    {
        auto object = new Position();
        auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
        auto anotherSut = std::move(sut);

        EXPECT_FALSE(m_deleterCalled);
        EXPECT_FALSE(sut);
        EXPECT_EQ(anotherSut.get(), object);
    }
    EXPECT_TRUE(m_deleterCalled);
}

TEST_F(UniquePtrTest, MoveAssignmentOverwriteAUniquePtrWithAnotherOne)
{
    {
        auto object = new Position();
        auto anotherObject = new Position();
        auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
        auto anotherSut = iox::cxx::unique_ptr<Position>(anotherObject, anotherDeleter);

        anotherSut = std::move(sut);

        EXPECT_TRUE(m_anotherDeleterCalled);
        EXPECT_FALSE(m_deleterCalled);
        EXPECT_FALSE(sut);
        EXPECT_EQ(anotherSut.get(), object);
    }
    EXPECT_TRUE(m_deleterCalled);
}

TEST_F(UniquePtrTest, CtorWithObjectPtrToNullAndDeleterSetsPtrToObjectAndDoesntCallsDeleter)
{
    {
        Position* object = nullptr;
        auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
        EXPECT_FALSE(sut);
        EXPECT_EQ(sut.get(), object);
    }

    EXPECT_FALSE(m_deleterCalled);
}

TEST_F(UniquePtrTest, AccessUnderlyingObject)
{
    auto object = new Position();
    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);

    EXPECT_EQ(sut->x, X_POS);
}

TEST_F(UniquePtrTest, ReleaseAnObject)
{
    auto object = new Position();
    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);

    EXPECT_EQ(sut.release(), object);
    EXPECT_FALSE(sut);
}

TEST_F(UniquePtrTest, ReleaseNullObject)
{
    Position* object = nullptr;
    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);

    EXPECT_EQ(sut.release(), object);
    EXPECT_FALSE(sut);
}

TEST_F(UniquePtrTest, ReleaseNullObject1)
{
    auto sut = iox::cxx::unique_ptr<Position>(nullptr);

    EXPECT_EQ(sut.release(), nullptr);
    EXPECT_FALSE(sut);
}

TEST_F(UniquePtrTest, ReleaseNullObject2)
{
    auto sut = iox::cxx::unique_ptr<Position>(deleter);

    EXPECT_EQ(sut.release(), nullptr);
    EXPECT_FALSE(sut);
}

TEST_F(UniquePtrTest, ResetToAnExistingRawPtr)
{
    auto object = new Position();
    auto anotherObject = new Position();

    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);

    sut.reset(anotherObject);

    EXPECT_TRUE(m_deleterCalled);
    EXPECT_EQ(sut.get(), anotherObject);
    EXPECT_TRUE(sut);
}

TEST_F(UniquePtrTest, SwapTwoValidUniquePtrsSucceeds)
{
    auto object = new Position();
    auto anotherObject = new Position();

    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
    auto anotherSut = iox::cxx::unique_ptr<Position>(anotherObject, anotherDeleter);

    sut.swap(anotherSut);

    EXPECT_FALSE(m_deleterCalled);
    EXPECT_EQ(sut.get(), anotherObject);
    EXPECT_EQ(anotherSut.get(), object);
    EXPECT_TRUE(sut);
    EXPECT_TRUE(anotherSut);
}


TEST_F(UniquePtrTest, SwapUniquePtrWithANullptrUniquePtrLeadsToDeletedUniquePtr)
{
    auto object = new Position();

    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
    auto anotherSut = iox::cxx::unique_ptr<Position>(nullptr);

    sut.swap(anotherSut);

    EXPECT_TRUE(m_deleterCalled);
    EXPECT_EQ(sut.get(), nullptr);
    EXPECT_FALSE(sut);
    EXPECT_FALSE(anotherSut);
}

TEST_F(UniquePtrTest, SwapUniquePtrWithADeleterOnlyUniquePtrLeadsToDeletedUniquePtr)
{
    auto object = new Position();

    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
    auto anotherSut = iox::cxx::unique_ptr<Position>(anotherDeleter);

    sut.swap(anotherSut);

    EXPECT_TRUE(m_deleterCalled);
    EXPECT_EQ(sut.get(), nullptr);
    EXPECT_FALSE(sut);
    EXPECT_FALSE(anotherSut);
}

TEST_F(UniquePtrTest, SwapANullptrUniquePtrWithUniquePtrLeadsToOneValidAndOneInvalidUniquePtrs)
{
    auto object = new Position();

    auto sut = iox::cxx::unique_ptr<Position>(nullptr);
    auto anotherSut = iox::cxx::unique_ptr<Position>(object, anotherDeleter);

    sut.swap(anotherSut);

    EXPECT_FALSE(m_deleterCalled);
    EXPECT_EQ(sut.get(), object);
    EXPECT_EQ(anotherSut.get(), nullptr);
    EXPECT_TRUE(sut);
    EXPECT_FALSE(anotherSut);
}

TEST_F(UniquePtrTest, SwapAADeleterOnlyUniquePtrWithUniquePtrLeadsToOneValidAndOneInvalidUniquePtrs)
{
    auto object = new Position();

    auto sut = iox::cxx::unique_ptr<Position>(deleter);
    auto anotherSut = iox::cxx::unique_ptr<Position>(object, anotherDeleter);

    sut.swap(anotherSut);

    EXPECT_FALSE(m_deleterCalled);
    EXPECT_EQ(sut.get(), object);
    EXPECT_EQ(anotherSut.get(), nullptr);
    EXPECT_TRUE(sut);
    EXPECT_FALSE(anotherSut);
}

TEST_F(UniquePtrTest, CompareAUniquePtrWithItselfIsTrue)
{
    auto object = new Position();

    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);

    EXPECT_TRUE(sut == sut);
}

TEST_F(UniquePtrTest, CompareAUniquePtrWithAnotherOneOfAnotherObjectIsFalse)
{
    auto object = new Position;
    auto anotherObject = new Position;

    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
    auto anotherSut = iox::cxx::unique_ptr<Position>(anotherObject, anotherDeleter);

    EXPECT_FALSE(sut == anotherSut);
}

TEST_F(UniquePtrTest, NotEqualCompareOfAUniquePtrWithItselfIsFalse)
{
    auto object = new Position();

    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);

    EXPECT_FALSE(sut != sut);
}

TEST_F(UniquePtrTest, NotEqualCompareOfAUniquePtrWithAnotherOneOfAnotherObjectIsTrue)
{
    auto object = new Position;
    auto anotherObject = new Position;

    auto sut = iox::cxx::unique_ptr<Position>(object, deleter);
    auto anotherSut = iox::cxx::unique_ptr<Position>(anotherObject, anotherDeleter);

    EXPECT_TRUE(sut != anotherSut);
}
