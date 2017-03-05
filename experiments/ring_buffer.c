/* standard library */
#include <assert.h> /* assert */
#include <stdlib.h> /* malloc, atoi */
#include <string.h> /* memset */
#include <stdio.h> /* printf */
#include <time.h> /* nanosleep */

/* external libraries */
#include <pthread.h>

#define MIN(x, y) (x) < (y) ? (x) : (y)

typedef int data_type;

struct ring_buffer
{
	unsigned int read_cursor;
	unsigned int write_cursor;
	unsigned int chunk_size;
	unsigned int buffer_size;
	pthread_mutex_t mutex;
	void *read_buffer;
	void *data;
};

void write_to_stdout(void *data, int chunks)
{
	data_type *typed_data = data;

	for (int i = 0; i < chunks; ++i)
		putchar(typed_data[i] ? '-' : '_');
}

void ring_buffer_print(struct ring_buffer *buffer)
{
	write_to_stdout(buffer->data, buffer->buffer_size);
	putchar('\n');
	for (unsigned int i = 0; i < buffer->buffer_size; ++i) {
		if (i == buffer->write_cursor && i == buffer->read_cursor) {
			putchar('|');
		} else if (i == buffer->write_cursor) {
			putchar('^');
		} else if (i == buffer->read_cursor) {
			putchar('\'');;
		} else {
			putchar(' ');
		}
	}
	putchar('\n');
}

void ring_buffer_read_test(struct ring_buffer *buffer)
{
	static const struct timespec delay = { 0, 500000000L };
	pthread_mutex_t *const mutex = &buffer->mutex;
	const int buffer_size = buffer->buffer_size;

	if (pthread_mutex_trylock(mutex) == 0) {
		const int read_cursor = buffer->read_cursor;
		const int write_cursor = buffer->write_cursor;
		const int chunk_size = buffer->chunk_size;
		const int cursor_diff = write_cursor - read_cursor;
		const int frames_to_end = buffer_size - read_cursor;

		int chunks_to_read = 0;

		if (cursor_diff < 0) {
			chunks_to_read = MIN(frames_to_end, chunk_size);
		} else if (cursor_diff > 0) {
			chunks_to_read = MIN(cursor_diff, chunk_size);
		}

		buffer->read_cursor = (read_cursor + chunks_to_read) % buffer_size;

		memcpy(buffer->read_buffer, buffer->data + read_cursor * sizeof(data_type), chunks_to_read * sizeof(data_type));
		pthread_mutex_unlock(mutex);

		if (chunks_to_read) {
#ifdef PRINT_WHOLE_BUFFER
			ring_buffer_print(buffer);
#else
			write_to_stdout(buffer->read_buffer, chunks_to_read);
#endif
		}

		nanosleep(&delay, NULL);
	}
}

void *ring_buffer_read_test_driver(void *context)
{
	while(1) ring_buffer_read_test(context);
}

int main(int argc, char **argv)
{
	void *memory;
	pthread_t read_thread;

	const unsigned int period = argc > 1 ? atoi(argv[1]) : 1;
	const unsigned int chunk_size = argc > 2 ? atoi(argv[2]) : 3;
	const unsigned int buffer_size = argc > 3 ? atoi(argv[3]) : 10;
	const unsigned int buffer_mem_size = buffer_size * sizeof(data_type);
	const unsigned int read_buffer_mem_size = chunk_size * sizeof(data_type);
	const unsigned int total_mem_size = buffer_mem_size + read_buffer_mem_size;

	struct ring_buffer buffer = {0};

	assert(memory = malloc(total_mem_size));
	memset(memory, 0, total_mem_size + 1);

	buffer.buffer_size = buffer_size;
	buffer.chunk_size = chunk_size;
	buffer.read_buffer = memory;
	buffer.data = memory + read_buffer_mem_size;

	assert(pthread_mutex_init(&buffer.mutex, NULL) == 0);
	assert(pthread_create(&read_thread, NULL, ring_buffer_read_test_driver, &buffer) == 0);

	int running_sample = 0;

	while (1) {
		pthread_mutex_t *const mutex = &buffer.mutex;

		if (pthread_mutex_trylock(mutex) == 0) {
			const unsigned int write_cursor = buffer.write_cursor;
			const unsigned int read_cursor = buffer.read_cursor;
			
			int samples_to_write;
			int region_one_cursor;
			int region_two_cursor;
			int region_one_size;
			int region_two_size;
			data_type *sample_ptr;

			if (write_cursor >= read_cursor) {
				samples_to_write = buffer_size - write_cursor + read_cursor - 1;
			} else {
				samples_to_write = read_cursor - write_cursor - 1;
			}

			if ((write_cursor + samples_to_write) >= buffer_size) {
				region_one_cursor = write_cursor;
				region_one_size = buffer_size - write_cursor;
				region_two_cursor = 0;
				region_two_size = samples_to_write - region_one_size;
			} else {
				region_one_cursor = write_cursor;
				region_one_size = samples_to_write;
				region_two_size = 0;
			}

			sample_ptr = buffer.data + (region_one_cursor * sizeof(data_type));
			for (int i = 0; i < region_one_size; ++i) {
				*sample_ptr++ = !((running_sample++ / period) % 2) ? 1 : 0;
			}

			sample_ptr = buffer.data + (region_two_cursor * sizeof(data_type));
			for (int i = 0; i < region_two_size; ++i) {
				*sample_ptr++ = !((running_sample++ / period) % 2) ? 1 : 0;
			}

			buffer.write_cursor = (write_cursor + region_one_size + region_two_size) % buffer_size;

			pthread_mutex_unlock(mutex);
			fflush(stdout);
		}
	}
}
