#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define MAX_CPUS 128
#define STATE_FILE "/tmp/tmux_cpu_state"
#define PROC_STAT "/proc/stat"

// Shared memory structure
typedef struct {
	int cpu_count;
	unsigned long long work[MAX_CPUS];
	unsigned long long total[MAX_CPUS];
} shared_state_t;

// CPU time snapshot
typedef struct {
	unsigned long long total;
	unsigned long long work;
} cpu_snapshot_t;

// Comparison function for qsort (descending order)
static inline int compare_desc(const void *a, const void *b) {
	return (*(const int *)b - *(const int *)a);
}

// Print a colorized block based on CPU usage percentage
static inline void print_colorized(int usage) {
	// Transparent (space) for very low usage - tmux status bar usually already green
	if (usage < 10) { 
		putchar(' ');
		return;
	}

	// Clamp usage to valid range
	if (usage > 100) usage = 100;

	int red, green;
	if (usage < 30) {
		// 10-30%: Yellow (#ffff00)
		red = 255;
		green = 255;
	} else if (usage < 50) {
		// 30-50%: Yellow to Orange
		// Transition: #ffff00 -> #ff8000
		red = 255;
		green = 255 - (((usage - 30) * 127) / 20);  // 255 -> 128
	} else if (usage < 70) {
		// 50-70%: Orange to Red-Orange
		// Transition: #ff8000 -> #ff4000
		red = 255;
		green = 128 - (((usage - 50) * 64) / 20);  // 128 -> 64
	} else {
		// 70-100%: Red-Orange to Pure Red
		// Transition: #ff4000 -> #ff0000
		red = 255;
		green = 64 - (((usage - 70) * 64) / 30);  // 64 -> 0
		if (green < 0) green = 0;
	}

	printf("#[fg=#%02x%02x00]█#[default]", red, green);
}

int main(void) {
	static char buf[4096];
	cpu_snapshot_t current_stats[MAX_CPUS];
	int usages[MAX_CPUS];
	int cpu_count = 0;

	// Read /proc/stat with single system call
	int fd = open(PROC_STAT, O_RDONLY);
	if (fd < 0) return 1;

	ssize_t bytes_read = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (bytes_read <= 0) return 1;
	buf[bytes_read] = '\0';

	// Parse CPU stats from buffer
	char *line = buf;
	while (*line && cpu_count < MAX_CPUS) {
		// Skip to cpu lines (cpu0, cpu1, etc.)
		if (line[0] == 'c' && line[1] == 'p' && line[2] == 'u' && 
			line[3] >= '0' && line[3] <= '9') {

			unsigned long long vals[8];

			// Fast parsing: skip "cpuN "
			while (*line && *line != ' ') line++;
			line++;

			// Parse 8 numbers
			for (int i = 0; i < 8; i++) {
				vals[i] = 0;
				while (*line >= '0' && *line <= '9') {
					vals[i] = vals[i] * 10 + (*line - '0');
					line++;
				}
				line++;
			}

			// Calculate work and total
			current_stats[cpu_count].work = vals[0] + vals[1] + vals[2] + vals[5] + vals[6] + vals[7];
			current_stats[cpu_count].total = current_stats[cpu_count].work + vals[3] + vals[4];
			cpu_count++;
		}

		// Skip to next line
		while (*line && *line != '\n') line++;
		if (*line == '\n') line++;
	}

	if (cpu_count == 0) return 1;

	// Open or create shared memory
	fd = open(STATE_FILE, O_RDWR | O_CREAT, 0600);
	if (fd < 0) return 1;

	// Ensure file is correct size
	struct stat st;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return 1;
	}

	if (st.st_size != sizeof(shared_state_t)) {
		if (ftruncate(fd, sizeof(shared_state_t)) < 0) {
			close(fd);
			return 1;
		}
	}

	// Memory-map the state file
	shared_state_t *state = mmap(NULL, sizeof(shared_state_t), 
							  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);  // Can close fd after mmap

	if (state == MAP_FAILED) return 1;

	// Calculate usage percentages using previous state
	int valid_prev = (state->cpu_count == cpu_count);

	for (int i = 0; i < cpu_count; i++) {
		if (valid_prev) {
			unsigned long long total_diff = current_stats[i].total - state->total[i];
			unsigned long long work_diff = current_stats[i].work - state->work[i];
			usages[i] = (total_diff > 0) ? (int)((work_diff * 100) / total_diff) : 0;
		} else {
			usages[i] = 0;
		}
	}

	// Update shared state with current values
	state->cpu_count = cpu_count;
	for (int i = 0; i < cpu_count; i++) {
		state->work[i] = current_stats[i].work;
		state->total[i] = current_stats[i].total;
	}

	// Unmap shared memory (changes persist)
	munmap(state, sizeof(shared_state_t));

	// Sort and output
	qsort(usages, cpu_count, sizeof(int), compare_desc);

	for (int i = 0; i < cpu_count; i++) {
		print_colorized(usages[i]);
	}

	return 0;
}
