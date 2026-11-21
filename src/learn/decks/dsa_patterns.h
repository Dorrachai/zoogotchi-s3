#pragma once
#include "../study_decks.h"

// =============================================================================
// 1. LeetCode / NeetCode Patterns (24 cards)
// =============================================================================
static const StudyCard kDeckDsaPatterns[] = {
    {
        "Contains Duplicate: Check if any int repeats in O(n) time.",
        {"Hash Set", "Sort + Nested Loop", "Min-Heap", "Trie"},
        0, "Check x in seen set; add x. Time O(n), Space O(n).", "Hash Set"
    },
    {
        "Valid Anagram: Check if strings s and t use identical letter counts.",
        {"2-Pointer Inward", "Char Freq Map", "Monotonic Stack", "Union Find"},
        1, "Count chars in dict/26-int array; verify same lengths.", "Freq Map"
    },
    {
        "Two Sum (unsorted): Find two indices adding up to target in O(n).",
        {"Binary Search", "Complement Hash Map", "Prefix Product", "Sliding Window"},
        1, "Store val->idx in seen; look up (target - num).", "Complement Map"
    },
    {
        "Group Anagrams: Which dict key groups anagrams in Python?",
        {"list(count)", "tuple(count26)", "sum(ord)", "set(word)"},
        1, "Lists are unhashable; tuple(count) gives O(N*K) grouping.", "Tuple Key"
    },
    {
        "Encode & Decode Strings safely even if words contain '#':",
        {"CSV + replace", "Length-prefix 4#str", "XOR each char", "Sort words"},
        1, "Prefix length + '#' lets decode jump exactly L chars.", "Length-Prefix"
    },
    {
        "Top K Frequent Elements in O(n) worst-case time (no log n):",
        {"Sort counts", "Max-Heap", "Bucket Sort by Freq", "Quickselect Only"},
        2, "Index buckets[freq]; walk backwards until k items found.", "Bucket Sort"
    },
    {
        "Product of Array Except Self in O(n) WITHOUT division:",
        {"Prefix & Suffix", "Two Pointers", "XOR Mask", "Monotonic Queue"},
        0, "Left pass stores prefix prod; right pass multiplies suffix.", "Prefix Suffix"
    },
    {
        "Valid Sudoku: Key to identify each 3x3 sub-box at (r, c):",
        {"(r%3, c%3)", "(r//3, c//3)", "r*3 + c", "(r+c)//3"},
        1, "Integer floor division (r//3, c//3) hashes 9 boxes cleanly.", "Sub-box Hash"
    },
    {
        "Longest Consecutive Sequence in O(n): How identify sequence start?",
        {"Sort array first", "(n - 1) not in set", "Use Min-Heap", "Prefix sums"},
        1, "Only count upward from n when (n - 1) is absent in set.", "Seq Start Set"
    },
    {
        "Valid Palindrome (alphanumeric only) in O(1) extra space:",
        {"Reverse copy", "Inward Two Pointers", "Hash Map", "Stack + Queue"},
        1, "Move L++ and R-- skipping non-alnum; compare lower().", "Two Pointers"
    },
    {
        "3Sum (unique triplets summing to 0) in O(n^2):",
        {"Triple loop", "Sort + Pin + 2Ptr", "Hash Set only", "Sliding Window"},
        1, "Sort, skip duplicate i, run 2-sum pointers on right.", "Sort + 2Ptr"
    },
    {
        "Container With Most Water: Which pointer moves inward each step?",
        {"Always move left", "Move shorter height", "Move taller height", "Move both"},
        1, "Width shrinks, so only raising the shorter wall can gain.", "Greedy 2Ptr"
    },
    {
        "Trapping Rain Water in O(1) space uses:",
        {"Running LMax / RMax", "2D DP grid", "Trie", "Topological Sort"},
        0, "Water at min(LMax, RMax) - h[i]; advance smaller max side.", "LMax RMax 2Ptr"
    },
    {
        "Longest Substring Without Repeating Chars uses:",
        {"Binary Search", "Var Sliding Window", "Prefix Product", "DFS Backtrack"},
        1, "Shrink left while s[r] in window set; track max r-l+1.", "Sliding Window"
    },
    {
        "Longest Repeating Char Replacement valid window condition:",
        {"len - maxFreq <= k", "len + maxFreq <= k", "unique <= k", "maxFreq >= k"},
        0, "Replacements needed = windowLen - mostFrequentCount <= k.", "Window MaxFreq"
    },
    {
        "Valid Parentheses matching order uses:",
        {"Queue FIFO", "LIFO Stack + Map", "Min-Heap", "Two Pointers"},
        1, "Push openers; on closer verify stack top matches closeMap.", "LIFO Stack"
    },
    {
        "Min Stack supporting getMin() in O(1) time:",
        {"Scan stack", "Synced Min Stack", "Sorted array", "Hash Table"},
        1, "Second stack tracks min(val, minStack[-1]) at each push.", "Min Stack"
    },
    {
        "Daily Temperatures (next warmer day index) in O(n):",
        {"Monotonic Dec Stack", "Max-Heap", "Binary Search", "BFS"},
        0, "Pop cooler indices off stack when warmer day arrives.", "Monotonic Stack"
    },
    {
        "Koko Eating Bananas (minimum valid speed k) uses:",
        {"Greedy sort", "BS on Answer Space", "Sliding Window", "Union Find"},
        1, "Monotonic hours(k) lets binary search over [1, max(piles)].", "BS Answer"
    },
    {
        "Search in Rotated Sorted Array: First step inside binary search?",
        {"Linear scan", "Check sorted half", "Re-sort array", "Use Hash Map"},
        1, "If nums[l] <= nums[mid], left is sorted; test target range.", "Rotated BS"
    },
    {
        "Shortest path in an unweighted graph or grid uses:",
        {"DFS recursion", "BFS Queue", "Union Find", "Monotonic Stack"},
        1, "BFS explores layer-by-layer so first hit is shortest.", "BFS Queue"
    },
    {
        "Course Schedule (detect cycle in directed graph) uses:",
        {"Binary Search", "3-Color DFS / Kahn", "Sliding Window", "Min-Heap"},
        1, "Kahn topological BFS or 3-state DFS catches back-edges.", "Cycle Topo"
    },
    {
        "Prefix autocomplete dictionary (insert, search, startsWith):",
        {"Trie (Prefix Tree)", "Adjacency Matrix", "Circular Buffer", "Disjoint Set"},
        0, "Each node maps char->child; O(L) per word operation.", "Trie Prefix"
    },
    {
        "Kth Largest Element in a live stream maintains:",
        {"Min-Heap size k", "Max-Heap size n", "Sorted list O(n)", "Stack"},
        0, "Min-Heap of size k keeps kth largest right at heap[0].", "Min-Heap K"
    },
};
