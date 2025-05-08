#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <getopt.h>
#include <signal.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <termios.h>
#include <sys/types.h>

// Constants
#define EXIT_SUCCESS        0
#define EXIT_FB_ERROR       1
#define EXIT_COLOR_ERROR    2
#define EXIT_GENERAL_ERROR  3

// Color definitions structure
typedef struct {
    unsigned int r;
    unsigned int g;
    unsigned int b;
} rgb_color;

// Function prototypes
void print_usage(const char* program_name);
int parse_color(const char* color_str, rgb_color* color, int color_depth);
int open_framebuffer(struct fb_var_screeninfo* vinfo, struct fb_fix_screeninfo* finfo);
void fill_screen(int fbfd, struct fb_var_screeninfo* vinfo, struct fb_fix_screeninfo* finfo, rgb_color* color);
int detect_color_depth(struct fb_var_screeninfo* vinfo);
void cleanup_and_exit(int sig);
int clear_active_display();
void save_display_state();
int is_ssh_session();
void read_current_color(struct fb_var_screeninfo* vinfo, struct fb_fix_screeninfo* finfo);

// Global variables for cleanup
char *original_fb = NULL;
long int screensize = 0;
int fbfd = -1;
struct termios original_term;
int console_fd = -1;
int non_interactive = 0;
int verbose = 0;
int from_daemon = 0;
int read_mode = 0;


void read_current_color(struct fb_var_screeninfo* vinfo, struct fb_fix_screeninfo* finfo) {
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
        fprintf(stderr, "Error: Could not open framebuffer device\n");
        return;
    }
    
    // Get current framebuffer info if not provided
    struct fb_var_screeninfo local_vinfo;
    struct fb_fix_screeninfo local_finfo;
    
    if (!vinfo || !finfo) {
        if (ioctl(fbfd, FBIOGET_VSCREENINFO, &local_vinfo) == -1 ||
            ioctl(fbfd, FBIOGET_FSCREENINFO, &local_finfo) == -1) {
            fprintf(stderr, "Error: Could not get framebuffer info\n");
            close(fbfd);
            return;
        }
        vinfo = &local_vinfo;
        finfo = &local_finfo;
    }
    
    // Map the framebuffer
    long int size = vinfo->yres_virtual * finfo->line_length;
    char *fbp = mmap(0, size, PROT_READ, MAP_SHARED, fbfd, 0);
    
    if ((long)fbp == -1) {
        fprintf(stderr, "Error: Failed to map framebuffer device to memory\n");
        close(fbfd);
        return;
    }
    
    // Initialize counters for each color
    unsigned long red_sum = 0, green_sum = 0, blue_sum = 0;
    unsigned long pixel_count = 0;
    
    // Sample every 100th pixel for performance
    const int sample_step = 100;
    
    // Check for running patch-generator instance first
    char state_path[256];
    snprintf(state_path, sizeof(state_path), "/tmp/patch-generator.pid");
    FILE *state_file = fopen(state_path, "r");
    int active_patch = (state_file != NULL);
    if (state_file) {
        fclose(state_file);
    }
    
    // Define fixed-size array for sample points
    #define MAX_QUICK_SAMPLES 10
    const int quick_samples = MAX_QUICK_SAMPLES;
    int samples[MAX_QUICK_SAMPLES][2];
    
    // Initialize the array with sample coordinates
    samples[0][0] = 0;                   // Top-left
    samples[0][1] = 0;
    samples[1][0] = vinfo->xres-1;       // Top-right
    samples[1][1] = 0;
    samples[2][0] = 0;                   // Bottom-left
    samples[2][1] = vinfo->yres-1;
    samples[3][0] = vinfo->xres-1;       // Bottom-right
    samples[3][1] = vinfo->yres-1;
    samples[4][0] = vinfo->xres/2;       // Center
    samples[4][1] = vinfo->yres/2;
    samples[5][0] = vinfo->xres/4;       // Quarter points
    samples[5][1] = vinfo->yres/4;
    samples[6][0] = vinfo->xres*3/4;
    samples[6][1] = vinfo->yres/4;
    samples[7][0] = vinfo->xres/4;
    samples[7][1] = vinfo->yres*3/4;
    samples[8][0] = vinfo->xres*3/4;
    samples[8][1] = vinfo->yres*3/4;
    samples[9][0] = vinfo->xres/3;
    samples[9][1] = vinfo->yres/3;
    
    int dark_count = 0;
    for (int i = 0; i < quick_samples; i++) {
        int x = samples[i][0];
        int y = samples[i][1];
        
        long int location = (x + vinfo->xoffset) * (vinfo->bits_per_pixel / 8) +
                           (y + vinfo->yoffset) * finfo->line_length;
        
        unsigned char b = 0, g = 0, r = 0;
        
        // Read pixel values based on bit depth
        if (vinfo->bits_per_pixel == 32) {
            b = *(fbp + location + 0);
            g = *(fbp + location + 1);
            r = *(fbp + location + 2);
        } else if (vinfo->bits_per_pixel == 16) {
            unsigned short pixel = *((unsigned short*)(fbp + location));
            
            // Extract color components based on offsets
            r = ((pixel >> vinfo->red.offset) & ((1 << vinfo->red.length) - 1)) 
                << (8 - vinfo->red.length);
            g = ((pixel >> vinfo->green.offset) & ((1 << vinfo->green.length) - 1))
                << (8 - vinfo->green.length);
            b = ((pixel >> vinfo->blue.offset) & ((1 << vinfo->blue.length) - 1))
                << (8 - vinfo->blue.length);
        } else {
            continue;  // Skip this pixel for unsupported bit depth
        }
        
        // Check if this pixel is very dark
        if (r < 20 && g < 20 && b < 20) {
            dark_count++;
        }
        
        // Also accumulate for full color detection
        red_sum += r;
        green_sum += g;
        blue_sum += b;
        pixel_count++;
    }
    
    // If no active patch and most sampled pixels are dark, consider it "off"
    if (!active_patch && dark_count >= quick_samples * 0.8) {
        printf("off\n");
        munmap(fbp, size);
        close(fbfd);
        return;
    }
    
    // Continue with full sampling if not determined to be off
    for (int y = 0; y < vinfo->yres; y += sample_step) {
        for (int x = 0; x < vinfo->xres; x += sample_step) {
            long int location = (x + vinfo->xoffset) * (vinfo->bits_per_pixel / 8) +
                               (y + vinfo->yoffset) * finfo->line_length;
            
            unsigned char b = 0, g = 0, r = 0;
            
            // Read pixel values based on bit depth
            if (vinfo->bits_per_pixel == 32) {
                b = *(fbp + location + 0);
                g = *(fbp + location + 1);
                r = *(fbp + location + 2);
            } else if (vinfo->bits_per_pixel == 16) {
                unsigned short pixel = *((unsigned short*)(fbp + location));
                
                // Extract color components based on offsets
                r = ((pixel >> vinfo->red.offset) & ((1 << vinfo->red.length) - 1)) 
                    << (8 - vinfo->red.length);
                g = ((pixel >> vinfo->green.offset) & ((1 << vinfo->green.length) - 1))
                    << (8 - vinfo->green.length);
                b = ((pixel >> vinfo->blue.offset) & ((1 << vinfo->blue.length) - 1))
                    << (8 - vinfo->blue.length);
            } else {
                // Unsupported bit depth - skip this pixel
                continue;
            }
            
            // Accumulate the values
            red_sum += r;
            green_sum += g;
            blue_sum += b;
            pixel_count++;
        }
    }
    
    // Clean up
    munmap(fbp, size);
    close(fbfd);
    
    // Check if we sampled any pixels
    if (pixel_count == 0) {
        printf("unknown (No pixels sampled)\n");
        return;
    }
    
    // Calculate average color
    unsigned char avg_r = red_sum / pixel_count;
    unsigned char avg_g = green_sum / pixel_count;
    unsigned char avg_b = blue_sum / pixel_count;
    
    // Check for overall darkness - if the average values are very low
    if (avg_r < 10 && avg_g < 10 && avg_b < 10) {
        if (!active_patch) {
            printf("off\n");
            return;
        } else {
            printf("black\n");
            return;
        }
    }
    
    // Normalize to 0 or 255 to detect solid colors
    // Allow some tolerance for compression artifacts or noise
    const int threshold = 200;  // High threshold for "on"
    const int low_threshold = 50;  // Low threshold for "off"
    
    int r_state = (avg_r > threshold) ? 1 : (avg_r < low_threshold ? 0 : -1);
    int g_state = (avg_g > threshold) ? 1 : (avg_g < low_threshold ? 0 : -1);
    int b_state = (avg_b > threshold) ? 1 : (avg_b < low_threshold ? 0 : -1);
    
    // Determine the color
    if (r_state == 1 && g_state == 0 && b_state == 0) {
        printf("red\n");
    } else if (r_state == 0 && g_state == 1 && b_state == 0) {
        printf("green\n");
    } else if (r_state == 0 && g_state == 0 && b_state == 1) {
        printf("blue\n");
    } else if (r_state == 0 && g_state == 1 && b_state == 1) {
        printf("cyan\n");
    } else if (r_state == 1 && g_state == 0 && b_state == 1) {
        printf("magenta\n");
    } else if (r_state == 1 && g_state == 1 && b_state == 0) {
        printf("yellow\n");
    } else if (r_state == 1 && g_state == 1 && b_state == 1) {
        printf("white\n");
    } else if (r_state == 0 && g_state == 0 && b_state == 0) {
        printf("black\n");
    } else {
        // For custom colors, output the RGB values
        printf("custom (rgb:%d,%d,%d)\n", avg_r, avg_g, avg_b);
    }
    
    // In verbose mode, print the actual averages
    if (verbose) {
        printf("Average RGB: %d,%d,%d\n", avg_r, avg_g, avg_b);
        printf("Active patch running: %s\n", active_patch ? "yes" : "no");
    }
}

// Detect if we're running in SSH
int is_ssh_session() {
    char *ssh_env = getenv("SSH_CLIENT") || getenv("SSH_TTY");
    if (ssh_env != NULL) return 1;
    
    // Check if we're in a pseudo-terminal
    char tty_name[256] = {0};
    if (ttyname_r(STDIN_FILENO, tty_name, sizeof(tty_name)) == 0) {
        if (strstr(tty_name, "pts") != NULL) {
            return 1;
        }
    }
    
    return 0;
}
int is_from_daemon() {
    // Check if we have no controlling terminal
    if (!isatty(STDIN_FILENO) && !isatty(STDOUT_FILENO)) {
        return 1;
    }
    
    // Check if parent process is a daemon or system service
    pid_t ppid = getppid();
    char proc_path[256];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/cmdline", ppid);
    
    FILE* f = fopen(proc_path, "r");
    if (f) {
        char cmdline[256] = {0};
        fread(cmdline, 1, sizeof(cmdline)-1, f);
        fclose(f);
        
        // Look for common daemon names/paths
        if (strstr(cmdline, "systemd") || 
            strstr(cmdline, "micropanel") || 
            strstr(cmdline, "daemon") ||
            strstr(cmdline, "d$")) {
            return 1;
        }
    }
    
    return 0;
}
void save_display_state() {
    // Create a state file
    char state_path[256];
    snprintf(state_path, sizeof(state_path), "/tmp/patch-generator.pid");

    FILE *state_file = fopen(state_path, "w");
    if (state_file) {
        fprintf(state_file, "%d\n", getpid());
        fclose(state_file);
    } else if (verbose) {
        perror("Warning: Failed to create state file");
    }
}

int clear_active_display() {
    // Read the state file
    char state_path[256];
    snprintf(state_path, sizeof(state_path), "/tmp/patch-generator.pid");

    FILE *state_file = fopen(state_path, "r");
    if (!state_file) {
        if (verbose) {
            fprintf(stderr, "No active color patch found.\n");
        }
        
        // Open framebuffer anyway to clear it
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;
        int fb = open_framebuffer(&vinfo, &finfo);
        if (fb >= 0) {
            rgb_color black = {0};
            fill_screen(fb, &vinfo, &finfo, &black);
            close(fb);
            return EXIT_SUCCESS;
        }
        
        return EXIT_GENERAL_ERROR;
    }

    // Read the PID
    pid_t patch_pid;
    if (fscanf(state_file, "%d", &patch_pid) != 1) {
        fclose(state_file);
        if (verbose) {
            fprintf(stderr, "Invalid state file.\n");
        }
        return EXIT_GENERAL_ERROR;
    }
    fclose(state_file);

    // Send signal to the process
    if (kill(patch_pid, SIGTERM) != 0) {
        if (verbose) {
            perror("Warning: Failed to terminate color patch process");
        }
        // Continue anyway to clean up
    }

    // Clean up state file
    unlink(state_path);

    if (verbose) {
        printf("Color patch cleared.\n");
    }
    return EXIT_SUCCESS;
}

void cleanup_and_exit(int sig) {
    if (verbose) {
        printf("\nRestoring screen...\n");
    }

    // Restore framebuffer content if we saved it
    if (original_fb && fbfd >= 0) {
        char *fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
        if ((long)fbp != -1) {
            memcpy(fbp, original_fb, screensize);
            munmap(fbp, screensize);
        }
        free(original_fb);
        original_fb = NULL;
    }

    // Restore terminal settings if not in non-interactive mode
    if (!non_interactive && console_fd >= 0) {
        // Re-enable cursor and text mode
        ioctl(console_fd, KDSETMODE, KD_TEXT);
        tcsetattr(console_fd, TCSANOW, &original_term);
        close(console_fd);
        console_fd = -1;
    }

    // Close framebuffer
    if (fbfd >= 0) {
        close(fbfd);
        fbfd = -1;
    }

    exit(sig == 0 ? EXIT_SUCCESS : EXIT_GENERAL_ERROR);
}

void print_usage(const char* program_name) {
    printf("Usage: %s [options] COLOR\n\n", program_name);
    printf("Options:\n");
    printf("  -d, --depth=DEPTH    Color depth (8, 10, or 'auto')\n");
    printf("  -t, --time=SECONDS   Display time in seconds (default: 0, infinite)\n");
    printf("  -x, --detach         Detach process and return to shell\n");
    printf("  -o, --off            Turn off currently active color patch\n");
    printf("  -n, --non-interactive  Non-interactive mode (for use by other services)\n");
    printf("  -r, --read           Read current display color\n");
    printf("  -v, --verbose        Output more information\n");
    printf("  -h, --help           Show this help message\n\n");
    printf("COLOR can be:\n");
    printf("  - Standard colors: red, green, blue, cyan, magenta, yellow, white, black\n");
    printf("  - RGB values in decimal: rgb:R,G,B (values depend on color depth)\n");
    printf("    For 8-bit: 0-255 per channel\n");
    printf("    For 10-bit: 0-1023 per channel\n\n");
    printf("Examples:\n");
    printf("  %s red\n", program_name);
    printf("  %s -d 10 -t 5 blue\n", program_name);
    printf("  %s --detach rgb:128,64,255\n", program_name);
    printf("  %s --off\n", program_name);
    printf("  %s --non-interactive red\n", program_name);
}

int parse_color(const char* color_str, rgb_color* color, int color_depth) {
    int max_val = (color_depth == 10) ? 1023 : 255;

    // Check for predefined colors
    if (strcmp(color_str, "red") == 0) {
        color->r = max_val;
        color->g = 0;
        color->b = 0;
    } else if (strcmp(color_str, "green") == 0) {
        color->r = 0;
        color->g = max_val;
        color->b = 0;
    } else if (strcmp(color_str, "blue") == 0) {
        color->r = 0;
        color->g = 0;
        color->b = max_val;
    } else if (strcmp(color_str, "cyan") == 0) {
        color->r = 0;
        color->g = max_val;
        color->b = max_val;
    } else if (strcmp(color_str, "magenta") == 0) {
        color->r = max_val;
        color->g = 0;
        color->b = max_val;
    } else if (strcmp(color_str, "yellow") == 0) {
        color->r = max_val;
        color->g = max_val;
        color->b = 0;
    } else if (strcmp(color_str, "white") == 0) {
        color->r = max_val;
        color->g = max_val;
        color->b = max_val;
    } else if (strcmp(color_str, "black") == 0) {
        color->r = 0;
        color->g = 0;
        color->b = 0;
    } else if (strncmp(color_str, "rgb:", 4) == 0) {
        // Parse custom RGB values
        if (sscanf(color_str + 4, "%u,%u,%u", &color->r, &color->g, &color->b) != 3) {
            if (verbose) {
                fprintf(stderr, "Error: Invalid RGB format. Use 'rgb:R,G,B'\n");
            }
            return -1;
        }

        // Validate ranges
        if (color->r > max_val || color->g > max_val || color->b > max_val) {
            if (verbose) {
                fprintf(stderr, "Error: RGB values must be between 0 and %d for %d-bit color\n",
                        max_val, color_depth);
            }
            return -1;
        }
    } else {
        if (verbose) {
            fprintf(stderr, "Error: Unknown color '%s'\n", color_str);
        }
        return -1;
    }

    return 0;
}

int detect_color_depth(struct fb_var_screeninfo* vinfo) {
    // Simple logic to determine if we're using 10-bit color
    // Most 10-bit framebuffers use 32 bits per pixel with 10 bits per RGB channel
    if (vinfo->bits_per_pixel == 32 &&
        vinfo->red.length == 10 &&
        vinfo->green.length == 10 &&
        vinfo->blue.length == 10) {
        return 10;
    }

    // Default to 8-bit
    return 8;
}

int open_framebuffer(struct fb_var_screeninfo* vinfo, struct fb_fix_screeninfo* finfo) {
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
        if (verbose) {
            perror("Error opening framebuffer device");
        }
        return -1;
    }

    // Get variable screen information
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, vinfo) == -1) {
        if (verbose) {
            perror("Error reading variable screen info");
        }
        close(fbfd);
        return -1;
    }

    // Get fixed screen information
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, finfo) == -1) {
        if (verbose) {
            perror("Error reading fixed screen info");
        }
        close(fbfd);
        return -1;
    }

    return fbfd;
}

void fill_screen(int fbfd, struct fb_var_screeninfo* vinfo, struct fb_fix_screeninfo* finfo, rgb_color* color) {
    screensize = vinfo->yres_virtual * finfo->line_length;
    char *fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    if ((long)fbp == -1) {
        if (verbose) {
            perror("Error: failed to map framebuffer device to memory");
        }
        return;
    }

    // Save original framebuffer content unless we're in non-interactive mode
    if (!non_interactive) {
        original_fb = malloc(screensize);
        if (original_fb) {
            memcpy(original_fb, fbp, screensize);
        } else if (verbose) {
            perror("Error: failed to allocate memory for framebuffer backup");
        }
    }

    long int location;
    unsigned int pixel_value;

    for (int y = 0; y < vinfo->yres; y++) {
        for (int x = 0; x < vinfo->xres; x++) {
            location = (x + vinfo->xoffset) * (vinfo->bits_per_pixel / 8) +
                       (y + vinfo->yoffset) * finfo->line_length;

            // Handle different bit depths
            if (vinfo->bits_per_pixel == 32) {
                *(fbp + location + 0) = color->b >> (vinfo->blue.length - 8);  // Blue
                *(fbp + location + 1) = color->g >> (vinfo->green.length - 8); // Green
                *(fbp + location + 2) = color->r >> (vinfo->red.length - 8);   // Red
                *(fbp + location + 3) = 0;                                     // Alpha/padding
            } else if (vinfo->bits_per_pixel == 16) {
                // 16-bit color (RGB565 usually)
                pixel_value = ((color->r >> 3) << vinfo->red.offset) |
                              ((color->g >> 2) << vinfo->green.offset) |
                              ((color->b >> 3) << vinfo->blue.offset);
                *((unsigned short*)(fbp + location)) = pixel_value;
            } else {
                // Handle other bit depths if needed
                if (verbose) {
                    fprintf(stderr, "Warning: Unsupported bit depth: %d\n", vinfo->bits_per_pixel);
                }
            }
        }
    }

    munmap(fbp, screensize);
}

int main(int argc, char* argv[]) {
    int color_depth = 0; // 0 = auto detect
    int display_time = 0; // 0 = indefinite
    int detach_mode = 0;
    int off_mode = 0;
    rgb_color color = {0};
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    int ssh_session = is_ssh_session();

    // Define long options
    static struct option long_options[] = {
        {"depth", required_argument, 0, 'd'},
        {"time",  required_argument, 0, 't'},
        {"detach",  no_argument, 0, 'x'},
        {"off",  no_argument, 0, 'o'},
        {"daemon",  no_argument, 0, 'D'},
        {"non-interactive", no_argument, 0, 'n'},
        {"read", no_argument, 0, 'r'},
        {"verbose", no_argument, 0, 'v'},
        {"help",  no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    // Set up signal handlers for clean exit
    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);

    // Parse command line options
    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "d:t:hxonvrD", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'd':
                if (strcmp(optarg, "auto") == 0) {
                    color_depth = 0;
                } else {
                    color_depth = atoi(optarg);
                    if (color_depth != 8 && color_depth != 10) {
                        fprintf(stderr, "Error: Color depth must be 8, 10, or 'auto'\n");
                        return EXIT_GENERAL_ERROR;
                    }
                }
                break;
            case 't':
                display_time = atoi(optarg);
                if (display_time < 0) {
                    fprintf(stderr, "Error: Display time must be >= 0\n");
                    return EXIT_GENERAL_ERROR;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            case 'x':
                detach_mode = 1;
                break;
            case 'o':
                off_mode = 1;
                break;
            case 'n':
                non_interactive = 1;
                break;
            case 'D':
                from_daemon = 1;
                non_interactive = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'r':
                read_mode = 1;
                break;
            default:
                print_usage(argv[0]);
                return EXIT_GENERAL_ERROR;
        }
    }

    // Handle the "off" mode early
    if (off_mode) {
        return clear_active_display();
    }

// After handling off_mode, add read_mode handling
if (read_mode) {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    // No need to open the framebuffer explicitly, the read_current_color function will do that
    read_current_color(NULL, NULL);
    return EXIT_SUCCESS;
}
// Now check if color is specified only for non-read, non-off modes
if (!read_mode && !off_mode && optind >= argc) {
    fprintf(stderr, "Error: No color specified\n");
    print_usage(argv[0]);
    return EXIT_GENERAL_ERROR;
}

    // Check if a color was specified
    if (optind >= argc) {
        fprintf(stderr, "Error: No color specified\n");
        print_usage(argv[0]);
        return EXIT_GENERAL_ERROR;
    }

    // In non-interactive mode, don't bother with console handling
    if (!non_interactive) {
        // Open the console for cursor control
        console_fd = open("/dev/tty", O_RDWR);
        if (console_fd >= 0) {
            // Save terminal settings
            tcgetattr(console_fd, &original_term);

            // Disable cursor and text input echo
            struct termios term;
            tcgetattr(console_fd, &term);
            term.c_lflag &= ~(ECHO | ICANON);
            tcsetattr(console_fd, TCSANOW, &term);

            // Switch to graphics mode
            ioctl(console_fd, KDSETMODE, KD_GRAPHICS);
            
            // Clear screen
            write(console_fd, "\033[2J\033[H", 7);
            
            // Try to hide cursor with ANSI escape sequence
            write(console_fd, "\033[?25l", 6);
        } else if (verbose && !ssh_session) {
            // Only warn if we're not in SSH and verbosity is enabled
            perror("Warning: Could not open console for cursor control");
        }
    }

    // Open the framebuffer device
    fbfd = open_framebuffer(&vinfo, &finfo);
    if (fbfd == -1) {
        if (!non_interactive) {
            cleanup_and_exit(1);
        }
        return EXIT_FB_ERROR;
    }
// Try direct console cursor control if in daemon mode
if (non_interactive || from_daemon) {
    // If we're in daemon mode, try more aggressive cursor hiding
    int vt_console;
    
    // Try various console devices
    const char* console_devices[] = {
        "/dev/tty0",
        "/dev/tty1",
        "/dev/console",
        "/dev/vc/0",
        NULL
    };
    
    for (int i = 0; console_devices[i] != NULL; i++) {
        vt_console = open(console_devices[i], O_RDWR);
        if (vt_console >= 0) {
            // Try various cursor hiding methods
            ioctl(vt_console, KDSETMODE, KD_GRAPHICS);
            write(vt_console, "\033[?25l", 6); // ANSI hide cursor
            
            // Try to blank the screen first (may help with cursor)
            ioctl(vt_console, FBIOBLANK, FB_BLANK_NORMAL);
            usleep(100000); // 100ms
            ioctl(vt_console, FBIOBLANK, FB_BLANK_UNBLANK);
            
            close(vt_console);
            break;
        }
    }
}

    // Detect color depth if not specified
    if (color_depth == 0) {
        color_depth = detect_color_depth(&vinfo);
        if (verbose) {
            printf("Auto-detected color depth: %d-bit\n", color_depth);
        }
    }

    // Parse color
    if (parse_color(argv[optind], &color, color_depth) != 0) {
        if (!non_interactive) {
            cleanup_and_exit(1);
        }
        return EXIT_COLOR_ERROR;
    }

    if (verbose) {
        printf("Displaying %s at %d-bit color depth\n", argv[optind], color_depth);
    }

    // Clear the framebuffer first
    if (console_fd >= 0) {
        // Force a screen refresh
        ioctl(fbfd, FBIOBLANK, 0);
    }

    // Fill screen with the specified color
    fill_screen(fbfd, &vinfo, &finfo, &color);

    // Handle detach mode
    if (detach_mode) {
        // Fork and detach
        pid_t pid = fork();
        if (pid > 0) {
            // Parent process exits immediately
            if (verbose) {
                printf("Color patch displayed. Use '%s --off' to restore terminal.\n", argv[0]);
            }
            
            // If in SSH, restore terminal for parent before exiting
            if (ssh_session && console_fd >= 0) {
                ioctl(console_fd, KDSETMODE, KD_TEXT);
                tcsetattr(console_fd, TCSANOW, &original_term);
                write(console_fd, "\033[?25h", 6); // Show cursor for SSH
            }
            
            exit(EXIT_SUCCESS);
        } else if (pid < 0) {
            // Fork failed
            if (verbose) {
                perror("Failed to fork process");
            }
            cleanup_and_exit(1);
        }

        // Child process continues
        setsid(); // Make process a session leader
        
        // Save state information
        save_display_state();
        
        // Close standard file descriptors to fully detach
        if (!verbose) {
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
        }

        // Wait for specified time or indefinitely
        if (display_time > 0) {
            sleep(display_time);
            // Clear screen and exit
            rgb_color black = {0};
            fill_screen(fbfd, &vinfo, &finfo, &black);
            if (console_fd >= 0) {
                ioctl(console_fd, KDSETMODE, KD_TEXT);
            }
            exit(EXIT_SUCCESS);
        } else {
            // Wait indefinitely or for a signal
            while (1) {
                sleep(60);
            }
        }
    }

    // For non-detached mode, wait if a display time was specified
    if (display_time > 0) {
        if (verbose) {
            printf("Display will close in %d seconds...\n", display_time);
        }
        sleep(display_time);
    } else if (!non_interactive) {
        if (verbose) {
            printf("Press any key to exit...\n");
        }
        // Wait for a keypress
        char buf[1];
        read(console_fd >= 0 ? console_fd : STDIN_FILENO, buf, 1);
    }

    // Clean up and exit
    cleanup_and_exit(0);

    return EXIT_SUCCESS; // Should not reach here
}
