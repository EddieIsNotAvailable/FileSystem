void readData(char *file, uint32_t startByte, uint32_t numBytes);
void set_attribute(uint32_t file_number, char* attr);
void list(char *param1, char *param2);
void retrieve(char *fileToRerieve, char *newFilename);
void createfs(char *filename);
void insert(char *filename);
void openfs(char *filename);
void closefs();
void savefs();
uint32_t df();
void init();