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

unsigned char buffer[BLKSIZE];

int main(int argc, char *argv[])
{
  fptr = fopen(inFile,"rb");
  write_ptr = fopen(outFile,"wb");

  fseek(fptr,0,SEEK_END);

  unsigned long len = (unsigned long)ftell(fptr); //total length of the file to be compressed
  unsigned long Nblks = len/BLKSIZE; //the number of needed blocks to compress the data
  unsigned long finalBytes = len%Nblks;
  
  printf("File length: %lu\n",len);
  
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
  //b3
  cl_mem b3_mem_obj; //the next block or last if there are two left
  b3_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      BLKSIZE * sizeof(char), NULL, &ret);
  //o2
  cl_mem o2_mem_obj; //compressed block from b2
  o2_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      BLKSIZE * sizeof(char), NULL, &ret);
  //o3
  cl_mem o3_mem_obj; //compressed block from b3
  o3_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      BLKSIZE * sizeof(char), NULL, &ret);
  //n1
  cl_mem n1_mem_obj; //length of first block
  n1_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      sizeof(int), NULL, &ret);
  //n2
  cl_mem n2_mem_obj; //length of second block //also the length of the compressed block on output
  n2_mem_obj  = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      sizeof(int), NULL, &ret);
  //n3
  cl_mem n3_mem_obj; //length of third block //also the length of the compressed block on output
  n3_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      sizeof(int), NULL, &ret);
  //fblk
  cl_mem fblk_mem_obj; //indicator of like first block final block ect.
  fblk_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      sizeof(short), NULL, &ret);
  short running = 1;
  if(Nblks < 3)
    running = 0;
  //first load the first one to two blocks to be loaded into memory and run in the right mode
  //make sure to load b1 is set to b3 
  if(Nblks == 1){ //fblk = 1
    //first and final block b2
    //use finalBytes
    //copy data to memory
    fread(buffer,finalBytes * sizeof(char),1,fptr);
    ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0,
			       finalBytes * sizeof(char), buffer, 0, NULL, NULL);
    //n2
    ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
			       sizeof(int), finalBytes, 0, NULL, NULL);
    //fblk
    ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0,
			       sizeof(short), 1, 0, NULL, NULL);
    //run program
    // Create a program from the kernel source
    cl_program program = clCreateProgramWithSource(context, 1, 
						   (const char **)&source_str,
						   (const size_t *)&source_size, &ret);

    // Build the program
    ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

    //create the kernel
    cl_kernel kernel = clCreateKernel(program,"lz77",&ret);

    //set the args of the kernel
    ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1
    ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2
    ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3
    ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2
    ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3
    ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1
    ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2
    ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3
    ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk

    // Execute the OpenCL kernel on the list
    size_t global_item_size = 2; // Process the entire lists //number of work groups
    //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length
    size_t local_item_size = MAX_WIN_SIZE; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024
    ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 
				 &global_item_size, &local_item_size, 0, NULL, NULL);
    //get output
    char *O2 = (char*)malloc((sizeof(char)*BLKSIZE));
    int N2;
    ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
			      sizeof(int), N2, 0, NULL, NULL);

    
    //write output to file
    fwrite(O2,sizeof(char),N2,write_ptr); //write to file
    //cleanup
    ret = clReleaseKernel(kernel);
    ret = clReleaseProgram(program);
    free(O2);
  }else if(Nblks == 2){ //fblk = 2
    //b2 first b3 final block
    //copy data to memory
    fread(buffer,sizeof(buffer),1,fptr);
    ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0,
			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
    fread(buffer,finalBytes * sizeof(char),1,fptr);
    ret = clEnqueueWriteBuffer(command_queue, b3_mem_obj, CL_TRUE, 0,
			       finalBytes * sizeof(char), buffer, 0, NULL, NULL);
    //n2
    ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
			       sizeof(int), BLKSIZE, 0, NULL, NULL);
    //n3
    ret = clEnqueueWriteBuffer(command_queue, n3_mem_obj, CL_TRUE, 0,
			       sizeof(int), finalBytes, 0, NULL, NULL);
    //fblk
    ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0,
			       sizeof(short), 2, 0, NULL, NULL);
    //run program
    // Create a program from the kernel source
    cl_program program = clCreateProgramWithSource(context, 1, 
						   (const char **)&source_str,
						   (const size_t *)&source_size, &ret);

    // Build the program
    ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

    //create the kernel
    cl_kernel kernel = clCreateKernel(program,"lz77",&ret);

    //set the args of the kernel
    ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1
    ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2
    ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3
    ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2
    ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3
    ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1
    ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2
    ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3
    ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk

    // Execute the OpenCL kernel on the list
    size_t global_item_size = 2; // Process the entire lists //number of work groups
    //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length
    size_t local_item_size = MAX_WIN_SIZE; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024
    ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 
				 &global_item_size, &local_item_size, 0, NULL, NULL);
    //get output
    char *O2 = (char*)malloc((sizeof(char)*BLKSIZE));
    char *O3 = (char*)malloc((sizeof(char)*BLKSIZE));
    int N2;
    int N3;
    ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
			      sizeof(int), N2, 0, NULL, NULL);
    ret = clEnqueueReadBuffer(command_queue, n3_mem_obj, CL_TRUE, 0,
			      sizeof(int), N3, 0, NULL, NULL);
    ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0,
			      BLKSIZE * sizeof(char), O2, 0, NULL, NULL);
    ret = clEnqueueReadBuffer(command_queue, o3_mem_obj, CL_TRUE, 0,
			      BLKSIZE * sizeof(char), O3, 0, NULL, NULL);
    
    //write output to file
    fwrite(O2,sizeof(char),N2,write_ptr); //write to file
    fwrite(O3,sizeof(char),N3,write_ptr); //write to file

    //cleanup
    ret = clReleaseKernel(kernel);
    ret = clReleaseProgram(program);
    free(O2);
    free(O3);
  }else {//fblk = 5
    //more blocks after
    //copy data to memory
    fread(buffer,sizeof(buffer),1,fptr);
    ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0,
			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
    fread(buffer,sizeof(buffer),1,fptr);
    ret = clEnqueueWriteBuffer(command_queue, b3_mem_obj, CL_TRUE, 0,
			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
    //n2
    ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
			       sizeof(int), BLKSIZE, 0, NULL, NULL);
    //n3
    ret = clEnqueueWriteBuffer(command_queue, n3_mem_obj, CL_TRUE, 0,
			       sizeof(int), BLKSIZE, 0, NULL, NULL);
    //fblk
    ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0,
			       sizeof(short), 5, 0, NULL, NULL);
    //run program
    // Create a program from the kernel source
    cl_program program = clCreateProgramWithSource(context, 1, 
						   (const char **)&source_str,
						   (const size_t *)&source_size, &ret);

    // Build the program
    ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

    //create the kernel
    cl_kernel kernel = clCreateKernel(program,"lz77",&ret);

    //set the args of the kernel
    ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1
    ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2
    ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3
    ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2
    ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3
    ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1
    ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2
    ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3
    ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk

    // Execute the OpenCL kernel on the list
    size_t global_item_size = 2; // Process the entire lists //number of work groups
    //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length
    size_t local_item_size = MAX_WIN_SIZE; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024
    ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 
				 &global_item_size, &local_item_size, 0, NULL, NULL);
    //get output
    char *O2 = (char*)malloc((sizeof(char)*BLKSIZE));
    char *O3 = (char*)malloc((sizeof(char)*BLKSIZE));
    int N2;
    int N3;
    ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
			      sizeof(int), N2, 0, NULL, NULL);
    ret = clEnqueueReadBuffer(command_queue, n3_mem_obj, CL_TRUE, 0,
			      sizeof(int), N3, 0, NULL, NULL);
    ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0,
			      BLKSIZE * sizeof(char), O2, 0, NULL, NULL);
    ret = clEnqueueReadBuffer(command_queue, o3_mem_obj, CL_TRUE, 0,
			      BLKSIZE * sizeof(char), O3, 0, NULL, NULL);
    
    //write output to file
    fwrite(O2,sizeof(char),N2,write_ptr); //write to file
    fwrite(O3,sizeof(char),N3,write_ptr); //write to file
    
    //set b1 to b3
    ret = clEnqueueWriteBuffer(command_queue, b1_mem_obj, CL_TRUE, 0,
			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
    //cleanup
    ret = clReleaseKernel(kernel);
    ret = clReleaseProgram(program);
    free(O2);
    free(O3);
  }
  
  while(running){
    if(Nblks > 2){ //fblk = 6
      //copy data to memory
      fread(buffer,sizeof(buffer),1,fptr);
      ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0,
				 BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
      fread(buffer,sizeof(buffer),1,fptr);
      ret = clEnqueueWriteBuffer(command_queue, b3_mem_obj, CL_TRUE, 0,
				 BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
      //n1
      ret = clEnqueueWriteBuffer(command_queue, n1_mem_obj, CL_TRUE, 0,
				 sizeof(int), BLKSIZE, 0, NULL, NULL);
      //n2
      ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				 sizeof(int), BLKSIZE, 0, NULL, NULL);
      //n3
      ret = clEnqueueWriteBuffer(command_queue, n3_mem_obj, CL_TRUE, 0,
				 sizeof(int), BLKSIZE, 0, NULL, NULL);
      //fblk
      ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0,
				 sizeof(short), 6, 0, NULL, NULL);
      //run program
      // Create a program from the kernel source
      cl_program program = clCreateProgramWithSource(context, 1, 
						     (const char **)&source_str,
						     (const size_t *)&source_size, &ret);

      // Build the program
      ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

      //create the kernel
      cl_kernel kernel = clCreateKernel(program,"lz77",&ret);

      //set the args of the kernel
      ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1
      ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2
      ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3
      ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2
      ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3
      ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1
      ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2
      ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3
      ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk

      // Execute the OpenCL kernel on the list
      size_t global_item_size = 2; // Process the entire lists //number of work groups
      //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length
      size_t local_item_size = MAX_WIN_SIZE; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024
      ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 
				   &global_item_size, &local_item_size, 0, NULL, NULL);
      //get output
      char *O2 = (char*)malloc((sizeof(char)*BLKSIZE));
      char *O3 = (char*)malloc((sizeof(char)*BLKSIZE));
      int N2;
      int N3;
      ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				sizeof(int), N2, 0, NULL, NULL);
      ret = clEnqueueReadBuffer(command_queue, n3_mem_obj, CL_TRUE, 0,
				sizeof(int), N3, 0, NULL, NULL);
      ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0,
				BLKSIZE * sizeof(char), O2, 0, NULL, NULL);
      ret = clEnqueueReadBuffer(command_queue, o3_mem_obj, CL_TRUE, 0,
				BLKSIZE * sizeof(char), O3, 0, NULL, NULL);
    
      //write output to file
      fwrite(O2,sizeof(char),N2,write_ptr); //write to file
      fwrite(O3,sizeof(char),N3,write_ptr); //write to file
    
      //set b1 to b3
      ret = clEnqueueWriteBuffer(command_queue, b1_mem_obj, CL_TRUE, 0,
				 BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
      //cleanup
      ret = clReleaseKernel(kernel);
      ret = clReleaseProgram(program);
      free(O2);
      free(O3);
    }else if( Nblks == 1 ) { //fblk = 4
      //copy data to memory
      fread(buffer,finalBytes * sizeof(char),1,fptr);
      ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0,
				 finalBytes * sizeof(char), buffer, 0, NULL, NULL);
      //n1
      ret = clEnqueueWriteBuffer(command_queue, n1_mem_obj, CL_TRUE, 0,
				 sizeof(int), BLKSIZE, 0, NULL, NULL);
      //n2
      ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				 sizeof(int), finalBytes, 0, NULL, NULL);
      //fblk
      ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0,
				 sizeof(short), 1, 0, NULL, NULL);
      //run program
      // Create a program from the kernel source
      cl_program program = clCreateProgramWithSource(context, 1, 
						     (const char **)&source_str,
						     (const size_t *)&source_size, &ret);

      // Build the program
      ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

      //create the kernel
      cl_kernel kernel = clCreateKernel(program,"lz77",&ret);

      //set the args of the kernel
      ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1
      ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2
      ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3
      ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2
      ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3
      ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1
      ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2
      ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3
      ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk

      // Execute the OpenCL kernel on the list
      size_t global_item_size = 2; // Process the entire lists //number of work groups
      //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length
      size_t local_item_size = WINSIZE; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024
      ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 
				   &global_item_size, &local_item_size, 0, NULL, NULL);
      //get output
      char *O2 = (char*)malloc((sizeof(char)*BLKSIZE));
      int N2;
      ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				sizeof(int), N2, 0, NULL, NULL);

    
      //write output to file
      fwrite(O2,sizeof(char),N2,write_ptr); //write to file

      running = 0;
      //cleanup
      ret = clReleaseKernel(kernel);
      ret = clReleaseProgram(program);
      free(O2);
    } else { //fblk = 3
      //copy data to memory
      fread(buffer,sizeof(buffer),1,fptr);
      ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0,
				 BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);
      fread(buffer,finalBytes * sizeof(char),1,fptr);
      ret = clEnqueueWriteBuffer(command_queue, b3_mem_obj, CL_TRUE, 0,
				 finalBytes * sizeof(char), buffer, 0, NULL, NULL);
      //n1
      ret = clEnqueueWriteBuffer(command_queue, n1_mem_obj, CL_TRUE, 0,
				 sizeof(int), BLKSIZE, 0, NULL, NULL);
      //n2
      ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				 sizeof(int), BLKSIZE, 0, NULL, NULL);
      //n3
      ret = clEnqueueWriteBuffer(command_queue, n3_mem_obj, CL_TRUE, 0,
				 sizeof(int), finalBytes, 0, NULL, NULL);
      //fblk
      ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0,
				 sizeof(short), 3, 0, NULL, NULL);
      //run program
      // Create a program from the kernel source
      cl_program program = clCreateProgramWithSource(context, 1, 
						     (const char **)&source_str,
						     (const size_t *)&source_size, &ret);

      // Build the program
      ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

      //create the kernel
      cl_kernel kernel = clCreateKernel(program,"lz77",&ret);

      //set the args of the kernel
      ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1
      ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2
      ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3
      ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2
      ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3
      ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1
      ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2
      ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3
      ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk

      // Execute the OpenCL kernel on the list
      size_t global_item_size = 2; // Process the entire lists //number of work groups
      //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length
      size_t local_item_size = MAX_WIN_SIZE; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024
      ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 
				   &global_item_size, &local_item_size, 0, NULL, NULL);
      //get output
      char *O2 = (char*)malloc((sizeof(char)*BLKSIZE));
      char *O3 = (char*)malloc((sizeof(char)*BLKSIZE));
      int N2;
      int N3;
      ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0,
				sizeof(int), N2, 0, NULL, NULL);
      ret = clEnqueueReadBuffer(command_queue, n3_mem_obj, CL_TRUE, 0,
				sizeof(int), N3, 0, NULL, NULL);
      ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0,
				BLKSIZE * sizeof(char), O2, 0, NULL, NULL);
      ret = clEnqueueReadBuffer(command_queue, o3_mem_obj, CL_TRUE, 0,
				BLKSIZE * sizeof(char), O3, 0, NULL, NULL);
    
      //write output to file
      fwrite(O2,sizeof(char),N2,write_ptr); //write to file
      fwrite(O3,sizeof(char),N3,write_ptr); //write to file
    
      running = 0; //this is the end
      //cleanup
      ret = clReleaseKernel(kernel);
      ret = clReleaseProgram(program);
      free(O2);
      free(O3);
    }
  }
  //all after this is old code and is just being ref'd currently
  
  /*******************************************************************************************************************************/
  /* cl_mem a_mem_obj;														 */
  /* cl_mem c_mem_obj;														 */
  /* if(BLKSIZE > len){														 */
  /*   //create memory buffer for the block											 */
  /*   a_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, 									 */
  /* 				      len * sizeof(char), NULL, &ret);								 */
  /*   c_mem_obj = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 									 */
  /* 				      len * sizeof(char), NULL, &ret);								 */
  /*   //copy the contents of the file to be compressed into the memory								 */
  /*   ret = clEnqueueWriteBuffer(command_queue, a_mem_obj, CL_TRUE, 0,								 */
  /* 			       len * sizeof(char), buffer, 0, NULL, NULL);							 */
  /* }else {															 */
  /*   //create memory buffer for the block											 */
  /* 																 */
  /*   a_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, 									 */
  /* 				      BLKSIZE * sizeof(char), NULL, &ret);							 */
  /*   c_mem_obj = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 									 */
  /* 				      BLKSIZE * sizeof(char), NULL, &ret);							 */
  /*   //copy the contents of the file to be compressed into the memory								 */
  /*   ret = clEnqueueWriteBuffer(command_queue, a_mem_obj, CL_TRUE, 0,								 */
  /* 			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL);							 */
  /* }																 */
  /* 																 */
  /* 																 */
  /* 																 */
  /* // Create a program from the kernel source											 */
  /* cl_program program = clCreateProgramWithSource(context, 1, 								 */
  /* 	      (const char **)&source_str, (const size_t *)&source_size, &ret);							 */
  /* 																 */
  /* // Build the program													 */
  /* ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);								 */
  /* 																 */
  /* //create the kernel													 */
  /* cl_kernel kernel = clCreateKernel(program,"add_one",&ret);									 */
  /* 																 */
  /* //set the args of the kernel												 */
  /* ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&a_mem_obj);								 */
  /* ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&c_mem_obj);								 */
  /* 																 */
  /* // Execute the OpenCL kernel on the list											 */
  /* size_t global_item_size = len; // Process the entire lists //number of work groups						 */
  /* //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length					 */
  /* size_t local_item_size = 26; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024	 */
  /* ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, 								 */
  /* 			       &global_item_size, &local_item_size, 0, NULL, NULL);						 */
  /* 																 */
  /* // Read the memory buffer C on the device to the local variable C								 */
  /* int header_offset = 5; //5 for no compression										 */
  /* char *C = (char*)malloc((sizeof(char)*len)+header_offset);									 */
  /* ret = clEnqueueReadBuffer(command_queue, c_mem_obj, CL_TRUE, 0, 								 */
  /*           len * sizeof(char), &C[header_offset], 0, NULL, NULL);								 */
  /* 																 */
  /* C[0] = 0x01;//three header bits followed by nothing for a uncompressed block						 */
  /* 																 */
  /* C[1] = 0x1A;														 */
  /* C[2] = 0x00;//number of bytes												 */
  /* 																 */
  /* C[3] = 0xE5;														 */
  /* C[4] = 0xFF;//two's compliment of number of bytes										 */
  /* 																 */
  /* fwrite(C,sizeof(char),len + header_offset,write_ptr); //write to file							 */
  /* 																 */
  /* //prints content of file in hex												 */
  /* for (int i = 0; i<len; ++i) {												 */
  /*   printf("%x\n",buffer[i]);												 */
  /* }																 */
  /* //fwrite(const void *ptr, size_t size_of_elements, size_t number_of_elements, FILE *a_file);				 */
  /* //fputc(0b1000,write_ptr);													 */
  /* for (int i = 0; i<len+header_offset; ++i) {										 */
  /*   printf("%x\n",C[i]);													 */
  /*   //fputc(C[i],write_ptr);													 */
  /* }																 */
  /*******************************************************************************************************************************/


  // Clean up
  ret = clFlush(command_queue);
  ret = clFinish(command_queue);
  ret = clReleaseMemObject(b1_mem_obj);
  ret = clReleaseMemObject(b2_mem_obj);
  ret = clReleaseMemObject(b3_mem_obj);
  ret = clReleaseMemObject(o2_mem_obj);
  ret = clReleaseMemObject(o3_mem_obj);
  ret = clReleaseMemObject(n1_mem_obj);
  ret = clReleaseMemObject(n2_mem_obj);
  ret = clReleaseMemObject(n3_mem_obj);
  ret = clReleaseMemObject(fblk_mem_obj);
  ret = clReleaseCommandQueue(command_queue);
  ret = clReleaseContext(context);
  fclose(fptr);
  fclose(write_ptr);
  return 0;
}

