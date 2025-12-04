#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_CPUS 128
#define STATE_FILE "/tmp/tmux_cpu_state.bin"

// Data Structure for CPU time snapshot
typedef struct {
	unsigned long long total;
	unsigned long long work;
} cpu_snapshot_t;

// Comparison function for qsort
int compare_desc(const void *a, const void *b) {
	return (*(int *)b - *(int *)a);
}

// void print_colorized(int usage) {
//     // Hide very low usage to keep the status bar clean
//     if (usage < 5) { 
//         printf(" ");
//         return;
//     }
//
//     int red, green;
//
//     if (usage <= 50) {
//         // 0% to 50%: Transition from Green (0,255,0) to Yellow (255,255,0)
//         // Maximize Green (255) and ramp up Red (0 to 255).
//         red = (usage * 255) / 50;
//         green = 255;
//     } else {
//         // 51% to 100%: Transition from Yellow (255,255,0) to Red (255,0,0)
//         // Maximize Red (255) and ramp down Green (255 to 0).
//         red = 255;
//         // The effective range is 51-100 (50 steps). 
//         green = 255 - ((usage - 50) * 255) / 50;
//     }
//
//     // Clamp values just in case
//     if (red < 0) red = 0; 
//     if (red > 255) red = 255;
//     if (green < 0) green = 0; 
//     if (green > 255) green = 255;
//
//     printf("#[fg=#%02x%02x00]█#[default]", red, green);
// }

void print_colorized(int usage) {
	// Hide very low usage to keep the status bar clean
	if (usage < 5) { 
		printf(" ");
		return;
	}

	int red = 255;
	int green;

	// We keep Red at maximum (255) for all usage levels.
	// The color is modulated by reducing Green from 255 down to 0.
	// 
	// 5% usage: Green is max (Yellow).
	// 100% usage: Green is min (Red).

	// Scale the usage (5% to 100%) to map to the Green color range (255 to 0).
	// The total range is 95 points (100 - 5).
	int clamped_usage = usage;
	if (clamped_usage > 100) clamped_usage = 100;
	if (clamped_usage < 5) clamped_usage = 5; // Should be caught by the initial check, but safe.

	// Map clamped_usage (5 to 100) to a reduction factor (0 to 255)
	// Example: 5% usage means 0 reduction (Green=255). 100% usage means 255 reduction (Green=0).
	int reduction_factor = ( (clamped_usage - 5) * 255 ) / 95;

	// Green starts at 255 (Yellow) and is reduced by the factor.
	green = 255 - reduction_factor;

	// Safety clamps
	if (green < 0) green = 0; 

	// Output is always Red and Green, with Blue set to 00.
	printf("#[fg=#%02x%02x00]█#[default]", red, green);
}

int main() {
	FILE *fp = fopen("/proc/stat", "r");
	if (!fp) return 1;

	char line[256];
	cpu_snapshot_t current_stats[MAX_CPUS];
	cpu_snapshot_t prev_stats[MAX_CPUS] = {0};
	int usages[MAX_CPUS];
	int cpu_count = 0;

	// Read /proc/stat and calculate current usage
	while (fgets(line, sizeof(line), fp) && cpu_count < MAX_CPUS) {
		// Only look for per-core lines (cpu0, cpu1, etc.)
		if (strncmp(line, "cpu", 3) == 0 && line[3] >= '0' && line[3] <= '9') {
			unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;

			// Parse all the required jiffies
			sscanf(line, "%*s %llu %llu %llu %llu %llu %llu %llu %llu",
		  &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);

			// Calculate total and work time for the current snapshot
			current_stats[cpu_count].work = user + nice + system + irq + softirq + steal;
			current_stats[cpu_count].total = current_stats[cpu_count].work + idle + iowait;
			cpu_count++;
		}
	}
	fclose(fp);

	// Load previous state and check for core count mismatch
	int saved_cpu_count = 0;
	FILE *state_fp = fopen(STATE_FILE, "rb");
	if (state_fp) {
		// Read the expected core count from the start of the file
		fread(&saved_cpu_count, sizeof(int), 1, state_fp); 

		// Only load stats if the current core count matches the saved count
		if (saved_cpu_count == cpu_count) {
			fread(prev_stats, sizeof(cpu_snapshot_t), cpu_count, state_fp);
		} else {
			// Core count mismatch (e.g., system rebooted). Clear previous stats.
			memset(prev_stats, 0, sizeof(cpu_snapshot_t) * MAX_CPUS);
		}
		fclose(state_fp);
	}

	// Calculate usage differences and prepare to save current state
	for (int i = 0; i < cpu_count; i++) {
		unsigned long long total_diff = current_stats[i].total - prev_stats[i].total;
		unsigned long long work_diff = current_stats[i].work - prev_stats[i].work;

		if (total_diff > 0) {
			// Calculate percentage using integer arithmetic
			usages[i] = (int)((work_diff * 100) / total_diff); 
		} else {
			usages[i] = 0;
		}
	}

	// Save current state (including core count) for the next run
	state_fp = fopen(STATE_FILE, "wb");
	if (state_fp) {
		fwrite(&cpu_count, sizeof(int), 1, state_fp); // Write the current count
		// Write all current core stats
		fwrite(&current_stats[0], sizeof(cpu_snapshot_t), cpu_count, state_fp); 
		fclose(state_fp);
	}

	// Sort (descending) and output
	qsort(usages, cpu_count, sizeof(int), compare_desc);

	for (int i = 0; i < cpu_count; i++) {
		print_colorized(usages[i]);
	}

	return 0;
}
