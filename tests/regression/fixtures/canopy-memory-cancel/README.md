# Disposable memory-recall cancellation experiment

Never install this fixture on hardware. A synthetic command 0x1d carrying an
LZ4 frame for one byte is sent directly to the stock dispatcher in DigiKit's
background worker task.
The test forces the reservation to fail, skips the loader, invokes the stock
UI's CANCEL RECALL handler, and checks whether the dispatcher returns and its
recall state changes. It also observes entry to stock temporary-buffer cleanup
and compares one edited Sound before and after. This is not an Overbridge
transport frame, does not run in the stock command task, and cannot prove
external failure signalling, complete project integrity, heap correctness,
or physical-device safety.
