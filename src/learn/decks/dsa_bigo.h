#pragma once
#include "../study_decks.h"

// =============================================================================
// 2. Big-O, Python Tips & Interview Edge Cases (16 cards)
// =============================================================================
static const StudyCard kDeckDsaBigO[] = {
    {
        "Average time for Python dict / set lookup (key in d):",
        {"O(log n)", "O(1)", "O(n)", "O(n log n)"},
        1, "Hash tables average O(1) lookup (O(n) worst collision).", "Dict O(1)"
    },
    {
        "Time complexity of Binary Search on sorted array of size n:",
        {"O(1)", "O(log n)", "O(n)", "O(sqrt(n))"},
        1, "Search space halves each step: O(log n) comparisons.", "O(log n) BS"
    },
    {
        "Time complexity of heappush / heappop on heap of size k:",
        {"O(1)", "O(log k)", "O(k)", "O(k log k)"},
        1, "Tree height is log2(k), so sift-up/down takes O(log k).", "Heap O(log k)"
    },
    {
        "Time complexity of heapq.heapify(nums) in-place on n items:",
        {"O(n)", "O(n log n)", "O(log n)", "O(n^2)"},
        0, "Bottom-up Floyd heapify runs in linear O(n) time!", "Heapify O(n)"
    },
    {
        "Merge Sort worst-case Time and Auxiliary Space complexity:",
        {"O(n log n) / O(1)", "O(n log n) / O(n)", "O(n^2) / O(1)", "O(n) / O(n)"},
        1, "Merge Sort guarantees O(n log n) time, needs O(n) temp.", "Merge Sort"
    },
    {
        "QuickSort worst-case time (already sorted + bad pivot):",
        {"O(n)", "O(n log n)", "O(n^2)", "O(log n)"},
        2, "Unbalanced partitions degrade QuickSort to O(n^2).", "QuickSort O(n^2)"
    },
    {
        "BFS and DFS time on adjacency-list graph (V nodes, E edges):",
        {"O(V * E)", "O(V + E)", "O(V^2)", "O(E log V)"},
        1, "Visits each vertex and adjacency edge once: O(V + E).", "Graph O(V+E)"
    },
    {
        "Dijkstra shortest path with binary min-heap:",
        {"O(V + E)", "O((V + E) log V)", "O(V^3)", "O(E^2)"},
        1, "Each edge relaxation can push to heap: O((V+E) log V).", "Dijkstra"
    },
    {
        "Python queue: Why use deque.popleft() instead of list.pop(0)?",
        {"popleft is O(1)", "list.pop(0) crashes", "deque sorts keys", "Saves disk"},
        0, "list.pop(0) shifts all elements O(n); deque.popleft O(1).", "Deque O(1)"
    },
    {
        "In a loop over n chars, s += ch repeatedly is:",
        {"O(1) total", "O(n) total", "O(n^2) total", "O(log n) total"},
        2, "Strings are immutable: append to list and ''.join() O(n).", "String Join"
    },
    {
        "Safer midpoint formula (avoids 32-bit int overflow in C++/Java):",
        {"(l + r) / 2", "l + (r - l) // 2", "r - l // 2", "(l * r) // 2"},
        1, "l + (r - l) // 2 never overflows INT_MAX.", "Midpoint Safe"
    },
    {
        "First 2 minutes of a Google SWE coding interview: What first?",
        {"Code optimal fast", "Clarify edge cases", "Skip brute force", "Import pandas"},
        1, "Ask empty array, single item, negatives, duplicates, bounds!", "Clarify First"
    },
    {
        "Before coding optimal solution, Google interviewers expect:",
        {"Silent typing", "Brute force + trade-off", "One-letter vars", "No dry-run"},
        1, "State naive O(N^2) baseline, then align on space/time gain.", "State Trade-off"
    },
    {
        "Detect a cycle in a Linked List using O(1) space:",
        {"Hash Set of nodes", "Fast & Slow Pointers", "Reverse list", "Recursion stack"},
        1, "Floyd tortoise & hare meet inside cycle with O(1) memory.", "Floyd Cycle"
    },
    {
        "Prefix sum lookup: Sum of subarray nums[l..r] inclusive:",
        {"pref[r] - pref[l]", "pref[r+1] - pref[l]", "pref[r] + pref[l]", "pref[r-1]"},
        1, "With 0-padded pref[0]=0, sum(l..r) = pref[r+1] - pref[l].", "Prefix Sum"
    },
    {
        "Final step after writing interview code:",
        {"Say done", "Trace 3-item dry-run", "Delete comments", "Rewrite in C"},
        1, "Walk a concrete 3-element trace line-by-line + Big-O!", "Dry-Run Trace"
    },
};
