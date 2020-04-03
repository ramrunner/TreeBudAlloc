# TreeBudAlloc
a space efficient tree based buddy allocator

# Design
Treebud alloc is a space efficient buddy allocator that only holds the state
in a bitfield. For example to have 16 different levels of halving allocation accurancy,
it would require around 16kbytes. the state of each cell is described by 2 bits, which
can be free (00), split (10) or full (11). Split means there is a subdivision of that space
further down the tree. The bitree is walked with recursive algorithms for freeing and allocing.

On freeing if succesfull the result is bubbled up the recursion with the hope that continuous
address space will be merged. No addresses are held, there is no other state apart
from the bittree. Also a small crude cli tool is provided to perform fake allocations, deallocs
and examine the allocator state. Please don't be that person that gives non page aligned space to
the allocator. noone likes that person.
