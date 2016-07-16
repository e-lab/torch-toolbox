int CreateWindow(const char *title, int x, int y, int w, int h);
void StartWindow();
int GetWindowSize(int *w, int *h);
int Blt(const void *image, int fmt, int w, int h, int dx, int dy, int dw, int dh);
int Present();
