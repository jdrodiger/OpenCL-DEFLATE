#include <stdio.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

//in bytes
#define BLKSIZE 65535
#define WINSIZE 32768
#define MAX_SOURCE_SIZE (0x100000)

char blkHeader; //three bits not necessarily at the byte boundary

char *inFile = "file";
char *outFile = "outFile.deflate";
char *lzfile = "lz77.cl"; 


FILE *fptr;//the file to compress
FILE *write_ptr;//the output compressed file
FILE *lzptr;//the lz77 opencl code
FILE *tstptr;//test pointer pls REMOVE

unsigned char buffer[BLKSIZE];

int main(int argc, char *argv[])
{
  fptr = fopen(inFile,"rb");
  write_ptr = fopen(outFile,"wb");

  fseek(fptr,0,SEEK_END);

  unsigned long len = (unsigned long)ftell(fptr); //total length of the file
  unsigned long Nblks = len/BLKSIZE; //the number of needed blocks to compress the data
  
  printf("File length: %lu\n",len);
  
  fseek(fptr,0,SEEK_SET);
  
  fread(buffer,sizeof(buffer),1,fptr);

  char *source_str;//string of the source to be loaded into opencl
  size_t source_size;
  tstptr = fopen("test_kernel.cl", "r"); //open the source file
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
  //b1
  cl_mem b1_mem_obj; //the previous block of data if it exists
  //b2
  cl_mem b2_mem_obj; //if its the last or the only block its this one
  //b3
  cl_mem b3_mem_obj; //the next block or last if there are two left
  //o2
  cl_mem o2_mem_obj; //compressed block from b2
  //o3
  cl_mem o3_mem_obj; //compressed block from b3
  //n1
  cl_mem n1_mem_obj; //length of first block
  //n2
  cl_mem n2_mem_obj; //length of second block //also the length of the compressed block on output
  //n3
  cl_mem n3_mem_obj; //length of third block //also the length of the compressed block on output
  //fblk
  cl_mem fblk_mem_obj; //indicator of like first block final block ect.

  //create objects in memory
  
  cl_mem a_mem_obj;
  cl_mem c_mem_obj;
  if(BLKSIZE > len){
    //create memory buffer for the block
    a_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, 
				      len * sizeof(char), NULL, &ret);
    c_mem_obj = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 
				      len * sizeof(char), NULL, &ret);
    //copy the contents of the file to be compressed into the memory
    ret = clEnqueueWriteBuffer(command_queue, a_mem_obj, CL_TRUE, 0,
			       len * sizeof(char), buffer, 0, NULL, NULL);
  }else {
    //create memory buffer for the block

    a_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, 
				      BLKSIZE * sizeof(char), NULL, &ret);
    c_mem_obj = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 
				      BLKSIZE * sizeof(char), NULL, &ret);
    //copy the contents of the file to be compressed into the memory
    ret = clEnqueueWriteBuffer(command_queue, a_mem_obj, CL_TRUE, 0,
			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
  }



  // Create a program from the kernel source
  cl_program program = clCreateProgramWithSource(context, 1, 
	      (const char **)&source_str, (const size_t *)&source_size, &ret);

  // Build the program
  ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

  //create the kernel
  cl_kernel kernel = clCreateKernel(program,"add_one",&ret);

  //set the args of the kernel
  ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&a_mem_obj);
  ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&c_mem_obj);

  // Execute the OpenCL kernel on the list
  size_t global_item_size = len; // Process the entire lists //number of work groups
  //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length
  size_t local_item_size = 26; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024
  ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 
			       &global_item_size, &local_item_size, 0, NULL, NULL);

  // Read the memory buffer C on the device to the local variable C
  int header_offset = 5; //5 for no compression
  char *C = (char*)malloc((sizeof(char)*len)+header_offset);
  ret = clEnqueueReadBuffer(command_queue, c_mem_obj, CL_TRUE, 0, 
            len * sizeof(char), &C[header_offset], 0, NULL, NULL);
  
  C[0] = 0x01;//three header bits followed by nothing for a uncompressed block
  
  C[1] = 0x1A;
  C[2] = 0x00;//number of bytes
  
  C[3] = 0xE5;
  C[4] = 0xFF;//two's compliment of number of bytes
  
  fwrite(C,sizeof(char),len + header_offset,write_ptr); //write to file

  //prints content of file in hex
  for (int i = 0; i<len; ++i) {
    printf("%x\n",buffer[i]);
  }
  //fwrite(const void *ptr, size_t size_of_elements, size_t number_of_elements, FILE *a_file);
  //fputc(0b1000,write_ptr);
  for (int i = 0; i<len+header_offset; ++i) {
    printf("%x\n",C[i]);
    //fputc(C[i],write_ptr);
  }


  // Clean up
  ret = clFlush(command_queue);
  ret = clFinish(command_queue);
  ret = clReleaseKernel(kernel);
  ret = clReleaseProgram(program);
  ret = clReleaseMemObject(a_mem_obj);
  ret = clReleaseMemObject(c_mem_obj);
  ret = clReleaseCommandQueue(command_queue);
  ret = clReleaseContext(context);
  free(C);
  fclose(fptr);
  fclose(write_ptr);
  return 0;
}

