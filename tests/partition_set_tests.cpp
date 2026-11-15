// PartitionSet and PartitionUsage: the small value types the Xbox app passes
// around instead of parallel bool arrays

#include <gtest/gtest.h>
#include "partition_format.h"
#include "partition_set.h"
#include "partition_usage.h"

namespace
{

const int LAST_PARTITION = XboxPartitionTableEntryCount - 1;

std::wstring PartitionList(const PartitionSet &partitions, size_t out_count)
{
    wchar_t text[100];
    FormatPartitionList(partitions, text, out_count);
    return text;
}

PartitionSet FirstStandardAndExtended()
{
    PartitionSet set;
    set.Add(0);
    set.Add(1);
    set.Add(XBOX_STANDARD_PARTITION_COUNT);
    return set;
}

FatxUsage SomeUsage()
{
    FatxUsage usage = {16384, 1234, 2, 5000};
    return usage;
}

} // namespace

TEST(PartitionSet, NewSetIsEmpty)
{
    EXPECT_TRUE(PartitionSet().IsEmpty());
}

TEST(PartitionSet, AddedPartitionIsContained)
{
    PartitionSet set;
    set.Add(LAST_PARTITION);
    EXPECT_TRUE(set.Contains(LAST_PARTITION));
}

TEST(PartitionSet, OtherPartitionsAreNotContained)
{
    EXPECT_FALSE(PartitionSet::Only(3).Contains(2));
}

TEST(PartitionSet, NegativeIndexIsNeverContained)
{
    EXPECT_FALSE(PartitionSet::Only(0).Contains(-1));
}

TEST(PartitionSet, IndexPastTheTableIsIgnored)
{
    EXPECT_TRUE(PartitionSet::Only(XboxPartitionTableEntryCount).IsEmpty());
}

TEST(PartitionSet, CountsItsMembers)
{
    EXPECT_EQ(FirstStandardAndExtended().Count(), 3);
}

TEST(PartitionSet, AddingTwiceCountsOnce)
{
    PartitionSet set = PartitionSet::Only(4);
    set.Add(4);
    EXPECT_EQ(set.Count(), 1);
}

TEST(PartitionSet, UnionHoldsBothSets)
{
    const PartitionSet both = PartitionSet::Only(2).Union(PartitionSet::Only(7));
    EXPECT_TRUE(both.Contains(2) && both.Contains(7) && both.Count() == 2);
}

TEST(FormatPartitionList, EmptySetIsEmptyText)
{
    EXPECT_EQ(PartitionList(PartitionSet(), 100), L"");
}

TEST(FormatPartitionList, ListsNumbersAndLetters)
{
    EXPECT_EQ(PartitionList(FirstStandardAndExtended(), 100), L"1 (E), 2 (C), 6 (F)");
}

TEST(FormatPartitionList, StopsBeforeAnItemThatDoesNotFit)
{
    EXPECT_EQ(PartitionList(FirstStandardAndExtended(), 14), L"1 (E), 2 (C)");
}

TEST(PartitionUsage, StartsUnknown)
{
    EXPECT_FALSE(PartitionUsage().IsKnown());
}

TEST(PartitionUsage, RememberedUsageIsKnown)
{
    PartitionUsage usage;
    usage.Remember(SomeUsage());
    EXPECT_TRUE(usage.IsKnown());
}

TEST(PartitionUsage, GetReturnsRememberedUsage)
{
    PartitionUsage usage;
    usage.Remember(SomeUsage());
    EXPECT_EQ(usage.Get().used_clusters, SomeUsage().used_clusters);
}

TEST(PartitionUsage, ForgottenUsageIsUnknown)
{
    PartitionUsage usage;
    usage.Remember(SomeUsage());
    usage.Forget();
    EXPECT_FALSE(usage.IsKnown());
}
