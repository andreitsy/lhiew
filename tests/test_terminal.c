#include "lhiew/types.h"
#include "lhiew/terminal.h"
#include "test_harness.h"

#include <errno.h>
#include <unistd.h>

typedef struct write_result {
    ssize_t count;
    int error;
} writeResult;

static const writeResult *results;
static size_t result_count, write_calls, output_length;
static size_t requests[8];
static char output[64];

/* Fault-injection entry point compiled into terminal_test_backend. */
ssize_t terminal_test_write(int fd, const void *buffer, size_t size);

ssize_t terminal_test_write(int fd, const void *buffer, size_t size) {
    size_t call = write_calls++;
    if (fd != STDOUT_FILENO || call >= result_count || call >= sizeof(requests) / sizeof(*requests)) {
        errno = EINVAL;
        return -1;
    }
    requests[call] = size;
    writeResult result = results[call];
    if (result.count < 0) {
        errno = result.error;
        return -1;
    }
    size_t count = (size_t)result.count;
    if (count > size || count > sizeof(output) - output_length) {
        errno = EOVERFLOW;
        return -1;
    }
    if (count)
        memcpy(output + output_length, buffer, count);
    output_length += count;
    return result.count;
}

static void reset_writes(const writeResult *sequence, size_t count) {
    results = sequence;
    result_count = count;
    write_calls = output_length = 0;
    memset(requests, 0, sizeof(requests));
    memset(output, 0, sizeof(output));
}

static void test_empty_write_accepts_null_without_a_syscall(void) {
    reset_writes(NULL, 0);
    errno = ENOSPC;
    ASSERT(terminal_write(NULL, 0));
    ASSERT_EQ(write_calls, (size_t)0);
    ASSERT_EQ(errno, ENOSPC);
}

static void test_full_write_preserves_binary_bytes(void) {
    const char data[] = {'A', '\0', 'B', '\xff', '\n'};
    const writeResult sequence[] = {{sizeof(data), 0}};
    reset_writes(sequence, 1);
    ASSERT(terminal_write(data, sizeof(data)));
    ASSERT_EQ(write_calls, (size_t)1);
    ASSERT_EQ(requests[0], sizeof(data));
    ASSERT_EQ(output_length, sizeof(data));
    ASSERT_EQ(memcmp(output, data, sizeof(data)), 0);
}

static void test_short_writes_advance_buffer_and_remaining_length(void) {
    const writeResult sequence[] = {{2, 0}, {1, 0}, {2, 0}};
    reset_writes(sequence, sizeof(sequence) / sizeof(*sequence));
    ASSERT(terminal_write("abcde", 5));
    ASSERT_EQ(write_calls, (size_t)3);
    ASSERT_EQ(requests[0], (size_t)5);
    ASSERT_EQ(requests[1], (size_t)3);
    ASSERT_EQ(requests[2], (size_t)2);
    ASSERT_EQ(output_length, (size_t)5);
    ASSERT_EQ(memcmp(output, "abcde", 5), 0);
}

static void test_interruptions_retry_without_skipping_or_repeating_bytes(void) {
    const writeResult sequence[] = {
        {-1, EINTR}, {2, 0}, {-1, EINTR}, {-1, EINTR}, {3, 0},
    };
    reset_writes(sequence, sizeof(sequence) / sizeof(*sequence));
    ASSERT(terminal_write("abcde", 5));
    ASSERT_EQ(write_calls, (size_t)5);
    ASSERT_EQ(requests[0], (size_t)5);
    ASSERT_EQ(requests[1], (size_t)5);
    ASSERT_EQ(requests[2], (size_t)3);
    ASSERT_EQ(requests[3], (size_t)3);
    ASSERT_EQ(requests[4], (size_t)3);
    ASSERT_EQ(output_length, (size_t)5);
    ASSERT_EQ(memcmp(output, "abcde", 5), 0);
}

static void test_write_errors_stop_and_preserve_errno(void) {
    const int errors[] = {ENOSPC, EIO, EAGAIN};
    for (size_t i = 0; i < sizeof(errors) / sizeof(*errors); ++i) {
        const writeResult sequence[] = {{-1, errors[i]}};
        reset_writes(sequence, 1);
        ASSERT(!terminal_write("abcde", 5));
        ASSERT_EQ(errno, errors[i]);
        ASSERT_EQ(write_calls, (size_t)1);
        ASSERT_EQ(output_length, (size_t)0);
    }
    const writeResult partial[] = {{2, 0}, {-1, EPIPE}};
    reset_writes(partial, sizeof(partial) / sizeof(*partial));
    ASSERT(!terminal_write("abcde", 5));
    ASSERT_EQ(errno, EPIPE);
    ASSERT_EQ(write_calls, (size_t)2);
    ASSERT_EQ(output_length, (size_t)2);
    ASSERT_EQ(memcmp(output, "ab", 2), 0);
}

static void test_zero_write_fails_without_retrying(void) {
    const writeResult zero[] = {{0, 0}};
    reset_writes(zero, 1);
    errno = EINTR;
    ASSERT(!terminal_write("abcde", 5));
    ASSERT_EQ(errno, EIO);
    ASSERT_EQ(write_calls, (size_t)1);
    ASSERT_EQ(output_length, (size_t)0);

    const writeResult partial[] = {{2, 0}, {0, 0}};
    reset_writes(partial, sizeof(partial) / sizeof(*partial));
    errno = EINTR;
    ASSERT(!terminal_write("abcde", 5));
    ASSERT_EQ(errno, EIO);
    ASSERT_EQ(write_calls, (size_t)2);
    ASSERT_EQ(output_length, (size_t)2);
    ASSERT_EQ(memcmp(output, "ab", 2), 0);
}

int main(void) {
    printf("test_terminal:\n");
    RUN_TEST(test_empty_write_accepts_null_without_a_syscall);
    RUN_TEST(test_full_write_preserves_binary_bytes);
    RUN_TEST(test_short_writes_advance_buffer_and_remaining_length);
    RUN_TEST(test_interruptions_retry_without_skipping_or_repeating_bytes);
    RUN_TEST(test_write_errors_stop_and_preserve_errno);
    RUN_TEST(test_zero_write_fails_without_retrying);
    TEST_REPORT();
}
