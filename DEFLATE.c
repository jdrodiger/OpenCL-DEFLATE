#include <stdio.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

//in bytes
#DEFINE BLKSIZE 65535
#DEFINE WINSIZE 32000
#DEFINE MAX_SOURCE_SIZE (0x100000)


char blkHeader; //three bits not necessarily at the byte boundary

char *inFile = "file";
char *outFile = "outFile";
char *lzfile = "lz77.cl";
char *huffile = "huffman.cl";


FILE *fptr;//the file to compress
FILE *write_ptr;//the output compressed file
FILE *lzptr;//the lz77 opencl code
FILE *hufptr;//the huffman opencl code

unsigned char buffer[BLKSIZE];

int main(int argc, char *argv[])
{
  fptr = fopen(inFile,"rb");
  write_ptr = fopen(outFile,"wb");

  fseek(fptr,0,SEEK_END);

  unsigned long len = (unsigned long)ftell(fptr);
  
  printf("%lu\n",len);
  fseek(fptr,0,SEEK_SET);
  fread(buffer,sizeof(buffer),1,fptr);
  for (int i = 0; i<len; ++i) {
    printf("%x\n",buffer[i]);
  }
  
  fclose(fptr);
  return 0;
}

