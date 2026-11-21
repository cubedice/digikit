// Request and response both use word 1 as idempotency key. One outstanding chunk.
#define RX_MAGIC 0x524c5831
#define RX_REPLY 0x524c5832
#define RX_PIN 1
#define RX_READ 2
#define RX_UNPIN 3
#define RX_LOAD 4
#define RX_WRITE 5
#define RX_COMMIT 6
#define RX_ABORT 7
#define RX_PEAK 9
#define RX_PEAKS 10
#define RX_PEAK_FRAMES 256
#define RX_FRAMES 10
#define RX_WORDS 16
