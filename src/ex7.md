# Exercise 7 – Concurrent Bag Analysis

## Linearizability
The concurrent bag implementation is not linearizable.
A failing `deq()` requires a moment where the bag is empty, but because queues are scanned sequentially, that moment may not exist.

## Counterexample
Two queues `Q0` and `Q1`.

Initial state:
- `Q0 = {x}`
- `Q1 = {y}`

Thread A performs `deq()`:

1. Checks `Q0` after another thread removed `x` → empty
2. Another thread moves `y` from `Q1` to `Q0`
3. Checks `Q1` → empty
4. Returns failure

The bag was never empty at any time during Thread A’s execution.

## Linearization Points
- `enq(v)`: LP of internal enqueue
- successful `deq(v)`: LP of internal dequeue
- failing `deq()`: no LP exists

## Sequential Consistency
The bag is sequentially consistent.
A total order consistent with per-thread program order can be constructed,
even if it violates real-time order.


