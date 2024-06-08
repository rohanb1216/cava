int print_raw_out(int bars_count, int fd, int is_binary, int bit_format, int ascii_range,
                  char bar_delim, char frame_delim, int const f[]);
int print_raw_windows_out(int bars_count, HANDLE hPipe, int is_binary, int bit_format,
                          int ascii_range,
						  char bar_delim, char frame_delim, int const f[]);