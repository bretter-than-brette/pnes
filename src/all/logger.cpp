// log() is entirely a macro on every platform, so nothing here needs a runtime
// definition -- and an earlier attempt to give logger.hpp silentHeapAmount and
// silentStackAmount weak defaults here could never work: pnes is a plain static
// archive, and archive linking only pulls in a member that resolves something
// already referenced. Nothing references this file, so it is never linked at all.
