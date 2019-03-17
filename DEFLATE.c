#include <stdio.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

//in bytes
#define BLKSIZE 65535
#define WINSIZE 32000
#define MAX_SOURCE_SIZE (0x100000)

char blkHeader; //three bits not necessarily at the byte boundary

char *inFile = "file";
char *outFile = "outFile";
char *lzfile = "lz77.cl";
char *huffile = "huffman.cl";

FILE *fptr;//the file to compress
FILE *write_ptr;//the output compressed file
FILE *lzptr;//the lz77 opencl code
FILE *hufptr;//the huffman opencl code
FILE *tstptr;//test pointer pls REMOVE

unsigned char buffer[BLKSIZE];

//this is in a loop:
  
  //step one read the contents in the file up to the block size and send to gpu
  
  //step two while the first block is processing be sending or reading the next
  //block of data to the gpu... also could be getting a previous  block back

  //step three after a block is read out of the gpu be sending it to disk

int main(int argc, char *argv[])
{
  fptr = fopen(inFile,"rb");
  write_ptr = fopen(outFile,"wb");

  fseek(fptr,0,SEEK_END);

  unsigned long len = (unsigned long)ftell(fptr); //total length of the file
  
  printf("File length: %lu\n",len);
  
  fseek(fptr,0,SEEK_SET);
  
  fread(buffer,sizeof(buffer),1,fptr);

  char *source_str;//string of the source to be loaded into opencl
  tstptr = fopen("vector_add_kernel.cl", "r"); //open the source file
  if (!tstptr) {
    fprintf(stderr, "Failed to load kernel.\n");
    exit(1);
  }
  source_str = (char*)malloc(MAX_SOURCE_SIZE);//allocate source string
  source_size = fread( source_str, 1, MAX_SOURCE_SIZE, tstptr); //read source
  fclose( tstptr ); //close file

  // Get platform and device information
  cl_platform_id platform_id = NULL;
  cl_device_id device_id = NULL;   
  cl_uint ret_num_devices;
  cl_uint ret_num_platforms;
  cl_int ret = clGetPlatformIDs(1, &platform_id, &ret_num_platforms);
  ret = clGetDeviceIDs( platform_id, CL_DEVICE_TYPE_GPU, 1, 
			&device_id, &ret_num_devices);

  // Create an OpenCL context
  cl_context context = clCreateContext( NULL, 1, &device_id, NULL, NULL, &ret);

  // Create a command queue
  cl_command_queue command_queue = clCreateCommandQueue(context, device_id, 0, &ret);

  cl_mem a_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, 
				    len * sizeof(char), NULL, &ret);

  //prints content of file in hex
  for (int i = 0; i<len; ++i) {
    printf("%x\n",buffer[i]);
    fputc(buffer[i],write_ptr);
  }
  
  fclose(fptr);
  fclose(write_ptr);
  return 0;
}

