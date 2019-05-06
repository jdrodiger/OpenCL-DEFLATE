
/**************************************************************************/
/* This program is free software: you can redistribute it and/or modify   */
/* it under the terms of the GNU General Public License as published by   */
/* the Free Software Foundation, either version 3 of the License, or      */
/* (at your option) any later version.                                    */
/*                                                                        */
/* This program is distributed in the hope that it will be useful,        */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of         */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          */
/* GNU General Public License for more details.                           */
/*                                                                        */
/* You should have received a copy of the GNU General Public License      */
/* along with this program.  If not, see <https://www.gnu.org/licenses/>. */
/**************************************************************************/
#include <stdio.h>
#include <unistd.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

//in bytes
#define BLKSIZE 65536
#define WINSIZE 32768
#define MAX_SOURCE_SIZE (0x100000)


char *inFile = "txtfile";
char *outFile = "outFile.deflate";
char *lzfile = "lz77.cl"; 


FILE *fptr;//the file to compress
FILE *write_ptr;//the output compressed file
FILE *lzptr;//the lz77 opencl code

unsigned char buffer[BLKSIZE];
unsigned char pshiftAmnt = 0;//the extra bits at the end of the previous block
unsigned char pbyte;//the last byte of the newly shifted block if there are more blocks after
unsigned char shifted = 0;

int main(int argc, char *argv[])
{
  if(argc != 3){
    printf("ERROR: invalid input\nUsage:\n  DEFLATE [input file] [output file]");
    return 1;
  }
  inFile = argv[1];
  outFile = argv[2];
  if(access( inFile, F_OK ) == -1){
    printf("ERROR: input file does not exist\n");
    return 1;
  }
  
  fptr = fopen(inFile,"rb");
  write_ptr = fopen(outFile,"wb");

  fseek(fptr,0,SEEK_END);

  unsigned long len = (unsigned long)ftell(fptr); //total length of the file to be compressed
  unsigned long Nblks = ((len-1)/BLKSIZE)+1; //the number of needed blocks to compress the data
  unsigned long finalBytes = len % BLKSIZE;
  unsigned long blksize = BLKSIZE;
  
  printf("Input file length: %lu\n",len);
  
  fseek(fptr,0,SEEK_SET);
  
  //fread(buffer,sizeof(buffer),1,fptr);

  char *source_str;//string of the source to be loaded into opencl
  size_t source_size;
  lzptr = fopen("lz77.cl", "r"); //open the source file
  if (!lzptr) {
    fprintf(stderr, "Failed to load kernel.\n");
    exit(1);
  }
  source_str = (char*)malloc(MAX_SOURCE_SIZE);//allocate source string
  source_size = fread( source_str, 1, MAX_SOURCE_SIZE, lzptr); //read source
  fclose( lzptr ); //close file
  

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
  b1_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      BLKSIZE * sizeof(char), NULL, &ret);
  //b2
  cl_mem b2_mem_obj; //if its the last or the only block its this one
  b2_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      BLKSIZE * sizeof(char), NULL, &ret);
  //o2
  cl_mem o2_mem_obj; //compressed block from b2
  o2_mem_obj = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
			      BLKSIZE * sizeof(char), NULL, &ret);
  //n1
  cl_mem n1_mem_obj; //length of first block
  n1_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      sizeof(int), NULL, &ret);
  //n2
  cl_mem n2_mem_obj; //length of second block //also the length of the compressed block on output
  n2_mem_obj  = clCreateBuffer(context, CL_MEM_READ_WRITE,
			      sizeof(int), NULL, &ret);
  //fblk
  cl_mem fblk_mem_obj; //indicator of like first block final block ect.
  fblk_mem_obj = clCreateBuffer(context, CL_MEM_READ_WRITE,
			      sizeof(short), NULL, &ret);

  short running = 1;
  short fblk = 10;
  //Nblks = 1;
  // if(Nblks < 3)
  //running = 0;

  
  //first load the first one to two blocks to be loaded into memory and run in the right mode
  //make sure to load b1 is set to b3
  while(running){
    printf("Blocks left to compress: %lu\n",Nblks);
    if(Nblks == 1){ //fblk = 1
      fblk = 1;
      //first and final block b2
      //copy data to memory

      fread(buffer,finalBytes * sizeof(char),1,fptr);
      ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0,
				 finalBytes * sizeof(char), buffer, 0, NULL, NULL);
      //n2
      ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				 sizeof(int), &finalBytes, 0, NULL, NULL);
      //fblk
      ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0,
				 sizeof(short), &fblk, 0, NULL, NULL);
      //run program
      // Create a program from the kernel source
      cl_program program = clCreateProgramWithSource(context, 1, 
						     (const char **)&source_str,
						     (const size_t *)&source_size, &ret);

      // Build the program
      ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);
      if(ret != CL_SUCCESS)
	printf("no build\n");

      //create the kernel
      cl_kernel kernel = clCreateKernel(program,"lz77",&ret);

      //set the args of the kernel
      ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1
      if(ret != CL_SUCCESS)
	printf("b1\n");
      ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2
      if(ret != CL_SUCCESS)
	printf("b2\n");
      ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&o2_mem_obj);//o2
      if(ret != CL_SUCCESS)
	printf("o2\n");
      ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&n1_mem_obj);//n1
      if(ret != CL_SUCCESS)
	printf("n1\n");
      ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&n2_mem_obj);//n2
      if(ret != CL_SUCCESS)
	printf("n2\n");
      ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk
      if(ret != CL_SUCCESS)
	printf("fblk\n");
      ret = clFinish(command_queue);

      // Execute the OpenCL kernel on the list
      size_t global_item_size = 1024;// * 2; // Process the entire lists //number of work groups
      //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length
      size_t local_item_size = 1024; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024
      ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 
				   &global_item_size, &local_item_size, 0, NULL, NULL);
      if(ret != CL_SUCCESS){
	printf("well that failed\n");
	if(ret == CL_INVALID_PROGRAM_EXECUTABLE) printf("invalid program executable\n");
	if(ret == CL_INVALID_COMMAND_QUEUE) printf("invalid cq\n");
	if(ret == CL_INVALID_KERNEL) printf("kernel\n");
	if(ret == CL_INVALID_CONTEXT) printf("context\n");
	if(ret == CL_INVALID_KERNEL_ARGS) printf("args\n");
	if(ret == CL_INVALID_WORK_DIMENSION) printf("word d\n");
	if(ret == CL_INVALID_GLOBAL_WORK_SIZE) printf("gwerksz\n");
	if(ret == CL_INVALID_WORK_GROUP_SIZE) printf("grpsz\n");
	if(ret == CL_INVALID_EVENT_WAIT_LIST) printf("waitlist\n");
	if(ret == CL_OUT_OF_HOST_MEMORY) printf("outofmem\n");
      }
      ret = clFinish(command_queue);
      //get output
      unsigned char *O2 = (unsigned char*)malloc((sizeof(char)*BLKSIZE));
      int N2;
      short sh;
      ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				sizeof(int), &N2, 0, NULL, NULL);
      if(ret != CL_SUCCESS)
	printf("outcopy fail\n");
      if(ret == CL_INVALID_COMMAND_QUEUE) printf("CL_INVALID_COMMAND_QUEUE\n");
      if(ret == CL_INVALID_CONTEXT) printf("CL_INVALID_CONTEXT\n");
      if(ret == CL_INVALID_MEM_OBJECT) printf("CL_INVALID_MEM_OBJECT\n");
      if(ret == CL_INVALID_VALUE) printf("CL_INVALID_VALUE\n");
      if(ret == CL_INVALID_EVENT_WAIT_LIST) printf("CL_INVALID_EVENT_WAIT_LIST\n");
      if(ret == CL_MISALIGNED_SUB_BUFFER_OFFSET) printf("CL_MISALIGNED_SUB_BUFFER_OFFSET\n");
      if(ret == CL_DEVICE_MEM_BASE_ADDR_ALIGN) printf("CL_DEVICE_MEM_BASE_ADDR_ALIGN\n");
      if(ret == CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST) printf("CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST\n");
      if(ret == CL_MEM_OBJECT_ALLOCATION_FAILURE) printf("CL_MEM_OBJECT_ALLOCATION_FAILURE\n");
      if(ret == CL_OUT_OF_RESOURCES) printf("CL_OUT_OF_RESOURCES\n");
      if(ret == CL_OUT_OF_HOST_MEMORY) printf("CL_OUT_OF_HOST_MEMORY\n");
      ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0,
				BLKSIZE * sizeof(char), O2, 0, NULL, NULL);
      ret = clEnqueueReadBuffer(command_queue, fblk_mem_obj,CL_TRUE, 0,
				sizeof(short), &sh, 0, NULL, NULL);

      //      printf("n2 %d\n",N2);
      //      printf("o2 %c\n",O2[1]);
      //shift it

      if(pshiftAmnt != 0){
        //        printf("shifting bits by %d\n",pshiftAmnt);
        O2[N2] = 0;
	pbyte |= (O2[0]<<pshiftAmnt);
        //        printf("pbyte: %x\n",pbyte);
	fwrite(&pbyte,sizeof(char),1,write_ptr);
	O2[0] >>= 8 - pshiftAmnt;
	for(int i = 1;i<N2+1;i++){
	  O2[i-1] |= O2[i]<<pshiftAmnt;
          //          printf("O2 i-1 : %x\n",O2[i-1]);
	  O2[i] >>= 8 - pshiftAmnt;
	}
      }
      pshiftAmnt = sh;

      
      //write output to file
      fwrite(O2,sizeof(char),N2,write_ptr); //write to file
      //cleanup
      ret = clReleaseKernel(kernel);
      ret = clReleaseProgram(program);
      free(O2);
      running = 0;
    }else{
      fblk = 2;
      //first and final block b2
      //copy data to memory
      Nblks--;
      
      fread(buffer,blksize * sizeof(char),1,fptr);
      ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0,
				 BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
      //n2
      ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				 sizeof(int), &blksize, 0, NULL, NULL);
      //fblk
      ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0,
				 sizeof(short), &fblk, 0, NULL, NULL);
      //run program
      // Create a program from the kernel source
      cl_program program = clCreateProgramWithSource(context, 1, 
						     (const char **)&source_str,
						     (const size_t *)&source_size, &ret);

      // Build the program
      ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);
      if(ret != CL_SUCCESS)
	printf("no build\n");

      //create the kernel
      cl_kernel kernel = clCreateKernel(program,"lz77",&ret);

      //set the args of the kernel
      ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1
      if(ret != CL_SUCCESS)
	printf("b1\n");
      ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2
      if(ret != CL_SUCCESS)
	printf("b2\n");
      ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&o2_mem_obj);//o2
      if(ret != CL_SUCCESS)
	printf("o2\n");
      ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&n1_mem_obj);//n1
      if(ret != CL_SUCCESS)
	printf("n1\n");
      ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&n2_mem_obj);//n2
      if(ret != CL_SUCCESS)
	printf("n2\n");
      ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk
      if(ret != CL_SUCCESS)
	printf("fblk\n");
      ret = clFinish(command_queue);

      // Execute the OpenCL kernel on the list
      size_t global_item_size = 1024;// * 2; // Process the entire lists //number of work groups
      //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length
      size_t local_item_size = 1024; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024
      ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 
				   &global_item_size, &local_item_size, 0, NULL, NULL);
      if(ret != CL_SUCCESS){
	printf("well that failed\n");
	if(ret == CL_INVALID_PROGRAM_EXECUTABLE) printf("invalid program executable\n");
	if(ret == CL_INVALID_COMMAND_QUEUE) printf("invalid cq\n");
	if(ret == CL_INVALID_KERNEL) printf("kernel\n");
	if(ret == CL_INVALID_CONTEXT) printf("context\n");
	if(ret == CL_INVALID_KERNEL_ARGS) printf("args\n");
	if(ret == CL_INVALID_WORK_DIMENSION) printf("word d\n");
	if(ret == CL_INVALID_GLOBAL_WORK_SIZE) printf("gwerksz\n");
	if(ret == CL_INVALID_WORK_GROUP_SIZE) printf("grpsz\n");
	if(ret == CL_INVALID_EVENT_WAIT_LIST) printf("waitlist\n");
	if(ret == CL_OUT_OF_HOST_MEMORY) printf("outofmem\n");
      }
      ret = clFinish(command_queue);
      //get output
      unsigned char *O2 = (unsigned char*)malloc((sizeof(char)*BLKSIZE));
      int N2;
      short sh;
      ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				sizeof(int), &N2, 0, NULL, NULL);
      if(ret != CL_SUCCESS)
	printf("outcopy fail\n");
      if(ret == CL_INVALID_COMMAND_QUEUE) printf("CL_INVALID_COMMAND_QUEUE\n");
      if(ret == CL_INVALID_CONTEXT) printf("CL_INVALID_CONTEXT\n");
      if(ret == CL_INVALID_MEM_OBJECT) printf("CL_INVALID_MEM_OBJECT\n");
      if(ret == CL_INVALID_VALUE) printf("CL_INVALID_VALUE\n");
      if(ret == CL_INVALID_EVENT_WAIT_LIST) printf("CL_INVALID_EVENT_WAIT_LIST\n");
      if(ret == CL_MISALIGNED_SUB_BUFFER_OFFSET) printf("CL_MISALIGNED_SUB_BUFFER_OFFSET\n");
      if(ret == CL_DEVICE_MEM_BASE_ADDR_ALIGN) printf("CL_DEVICE_MEM_BASE_ADDR_ALIGN\n");
      if(ret == CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST) printf("CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST\n");
      if(ret == CL_MEM_OBJECT_ALLOCATION_FAILURE) printf("CL_MEM_OBJECT_ALLOCATION_FAILURE\n");
      if(ret == CL_OUT_OF_RESOURCES) printf("CL_OUT_OF_RESOURCES\n");
      if(ret == CL_OUT_OF_HOST_MEMORY) printf("CL_OUT_OF_HOST_MEMORY\n");
      ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0,
				BLKSIZE * sizeof(char), O2, 0, NULL, NULL);
      ret = clEnqueueReadBuffer(command_queue, fblk_mem_obj,CL_TRUE, 0,
				sizeof(short), &sh, 0, NULL, NULL);

      //      printf("n2 %d\n",N2);
      //      printf("o2 %c\n",O2[1]);

      if(pshiftAmnt != 0){
        //    printf("shifting bits by %d\n",pshiftAmnt);
        O2[N2] = 0;
	pbyte |= (O2[0]<<pshiftAmnt);
	fwrite(&pbyte,sizeof(char),1,write_ptr);
	O2[0] >>= 8 - pshiftAmnt;
	for(int i = 1;i<N2+1;i++){
	  O2[i-1] |= O2[i]<<pshiftAmnt;
          //printf("O2 i-1 : %x\n",O2[i-1]);
	  O2[i] >>= 8 - pshiftAmnt;
	}
	pbyte = O2[N2] >> pshiftAmnt;
      }else{
	pbyte = O2[N2];
      }
      pshiftAmnt = (sh + pshiftAmnt) % 8;
      //      printf("pbyte: %x\n",pbyte);

      //write output to file
      fwrite(O2,sizeof(char),N2-1,write_ptr); //write to file
      //cleanup
      ret = clReleaseKernel(kernel);
      ret = clReleaseProgram(program);
      free(O2);
    }
  }
  printf("output file length: %li\n",ftell(write_ptr));
  // Clean up
  ret = clFlush(command_queue);
  ret = clFinish(command_queue);
  ret = clReleaseMemObject(b1_mem_obj);
  ret = clReleaseMemObject(b2_mem_obj);
  //  ret = clReleaseMemObject(b3_mem_obj);
  ret = clReleaseMemObject(o2_mem_obj);
  //  ret = clReleaseMemObject(o3_mem_obj);
  ret = clReleaseMemObject(n1_mem_obj);
  ret = clReleaseMemObject(n2_mem_obj);
  //  ret = clReleaseMemObject(n3_mem_obj);
  ret = clReleaseMemObject(fblk_mem_obj);
  ret = clReleaseCommandQueue(command_queue);
  ret = clReleaseContext(context);
  fclose(fptr);
  fclose(write_ptr);
  return 0;
}

