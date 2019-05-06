#include <stdio.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

//in bytes
#define BLKSIZE 65535//65535//1310710
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

int main(int argc, char *argv[])
{
  fptr = fopen(inFile,"rb");
  write_ptr = fopen(outFile,"wb");

  fseek(fptr,0,SEEK_END);

  unsigned long len = (unsigned long)ftell(fptr); //total length of the file to be compressed
  unsigned long Nblks = ((len-1)/BLKSIZE)+1; //the number of needed blocks to compress the data
  unsigned long finalBytes = len % BLKSIZE;
  unsigned long blksize = BLKSIZE;
  printf("final bytes %lu \n",finalBytes);
  
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
  //  cl_mem b3_mem_obj; //the next block or last if there are two left
  //  b3_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
  //			      BLKSIZE * sizeof(char), NULL, &ret);
  //o2
  cl_mem o2_mem_obj; //compressed block from b2
  o2_mem_obj = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
			      BLKSIZE * sizeof(char), NULL, &ret);
  //o3
  //  cl_mem o3_mem_obj; //compressed block from b3
  //  o3_mem_obj = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
  //			      BLKSIZE * sizeof(char), NULL, &ret);
  //n1
  cl_mem n1_mem_obj; //length of first block
  n1_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
			      sizeof(int), NULL, &ret);
  //n2
  cl_mem n2_mem_obj; //length of second block //also the length of the compressed block on output
  n2_mem_obj  = clCreateBuffer(context, CL_MEM_READ_WRITE,
			      sizeof(int), NULL, &ret);
  //n3
  //  cl_mem n3_mem_obj; //length of third block //also the length of the compressed block on output
  //  n3_mem_obj = clCreateBuffer(context, CL_MEM_READ_WRITE,
  //			      sizeof(int), NULL, &ret);
  //fblk
  cl_mem fblk_mem_obj; //indicator of like first block final block ect.
  fblk_mem_obj = clCreateBuffer(context, CL_MEM_READ_WRITE,
			      sizeof(short), NULL, &ret);

  short running = 1;
  short fblk = 10;
  Nblks = 1;
  if(Nblks < 3)
    running = 0;
  
  //first load the first one to two blocks to be loaded into memory and run in the right mode
  //make sure to load b1 is set to b3
  printf("Nblks %lu\n",Nblks);
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
    //    ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3
    //    if(ret != CL_SUCCESS)
    //      printf("b3\n");
    ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&o2_mem_obj);//o2
    if(ret != CL_SUCCESS)
      printf("o2\n");
    //    ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3
    //    if(ret != CL_SUCCESS)
    //      printf("o3\n");
    ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&n1_mem_obj);//n1
    if(ret != CL_SUCCESS)
      printf("n1\n");
    ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&n2_mem_obj);//n2
    if(ret != CL_SUCCESS)
      printf("n2\n");
    //    ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3
    //    if(ret != CL_SUCCESS)
    //      printf("n3\n");
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
    char *O2 = (char*)malloc((sizeof(char)*BLKSIZE));
    int N2;
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

    printf("n2 %d\n",N2);
    printf("o2 %c\n",O2[1]);
    //write output to file
    fwrite(O2,sizeof(char),N2,write_ptr); //write to file
    //cleanup
    ret = clReleaseKernel(kernel);
    ret = clReleaseProgram(program);
    free(O2);
  }
    ///////////////////////////////////////////////////////////////////////////////////////////////////    
  /* }else if(Nblks == 2){ //fblk = 2 */
  /*   fblk = 2; */
  /*   //b2 first b3 final block */
    
  /*   //copy data to memory */
  /*   fread(buffer,sizeof(buffer),1,fptr); */
  /*   ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0, */
  /* 			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL); */
  /*   fread(buffer,finalBytes,1,fptr); */
  /*   ret = clEnqueueWriteBuffer(command_queue, b3_mem_obj, CL_TRUE, 0, */
  /* 			       finalBytes * sizeof(char), buffer, 0, NULL, NULL); */
  /*   //n2 */
  /*   ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 			       sizeof(int), &blksize, 0, NULL, NULL); */
  /*   //n3 */
  /*   ret = clEnqueueWriteBuffer(command_queue, n3_mem_obj, CL_TRUE, 0, */
  /* 			       sizeof(int), &finalBytes, 0, NULL, NULL); */
  /*   //fblk */
  /*   ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 			       sizeof(short), &fblk, 0, NULL, NULL); */
  /*   //run program */
  /*   // Create a program from the kernel source */
  /*   cl_program program = clCreateProgramWithSource(context, 1,  */
  /* 						   (const char **)&source_str, */
  /* 						   (const size_t *)&source_size, &ret); */

  /*   // Build the program */
  /*   ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL); */
  /*   if (ret != CL_SUCCESS) */
  /*   { */
  /*       size_t len; */
  /*       char buffer[204800]; */
  /*   cl_build_status bldstatus; */
  /*   //    printf("\nError %d: Failed to build program executable [ %s ]\n",ret,get_error_string(ret)); */
  /*   ret = clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_STATUS, sizeof(bldstatus), (void *)&bldstatus, &len); */
  /*   if (ret != CL_SUCCESS) */
  /*     { */
  /* 	//        printf("Build Status error %d: %s\n",ret,get_error_string(ret)); */
  /*       exit(1); */
  /*     }      */
  /*   if (bldstatus == CL_BUILD_SUCCESS) printf("Build Status: CL_BUILD_SUCCESS\n"); */
  /*   if (bldstatus == CL_BUILD_NONE) printf("Build Status: CL_BUILD_NONE\n");  */
  /*   if (bldstatus == CL_BUILD_ERROR) printf("Build Status: CL_BUILD_ERROR\n"); */
  /*   if (bldstatus == CL_BUILD_IN_PROGRESS) printf("Build Status: CL_BUILD_IN_PROGRESS\n");   */
  /*   ret = clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_OPTIONS, sizeof(buffer), buffer, &len); */
  /*   if (ret != CL_SUCCESS) */
  /*     { */
  /* 	//        printf("Build Options error %d: %s\n",ret,get_error_string(ret)); */
  /*       exit(1); */
  /*     }         */
  /*   printf("Build Options: %s\n", buffer);   */
  /*   ret = clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);     */
  /*   if (ret != CL_SUCCESS) */
  /*     { */
  /* 	//   printf("Build Log error %d: %s\n",ret,get_error_string(ret)); */
  /*       exit(1); */
  /*     }      */
  /*   printf("Build Log:\n%s\n", buffer); */
  /*   exit(1); */
  /*   } */

  /*   //create the kernel */
  /*   cl_kernel kernel = clCreateKernel(program,"lz77",&ret); */

  /*   //set the args of the kernel */
  /*   /\* ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1 *\/ */
  /*   /\* ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2 *\/ */
  /*   /\* ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3 *\/ */
  /*   /\* ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2 *\/ */
  /*   /\* ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3 *\/ */
  /*   /\* ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1 *\/ */
  /*   /\* ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2 *\/ */
  /*   /\* ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3 *\/ */
  /*   /\* ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk *\/ */
  /*   ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1 */
  /*   if(ret != CL_SUCCESS) */
  /*     printf("b1\n"); */
  /*   ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2 */
  /*   if(ret != CL_SUCCESS) */
  /*     printf("b2\n"); */
  /*   ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3 */
  /*   if(ret != CL_SUCCESS) */
  /*     printf("b3\n"); */
  /*   ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2 */
  /*   if(ret != CL_SUCCESS) */
  /*     printf("o2\n"); */
  /*   ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3 */
  /*   if(ret != CL_SUCCESS) */
  /*     printf("o3\n"); */
  /*   ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1 */
  /*   if(ret != CL_SUCCESS) */
  /*     printf("n1\n"); */
  /*   ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2 */
  /*   if(ret != CL_SUCCESS) */
  /*     printf("n2\n"); */
  /*   ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3 */
  /*   if(ret != CL_SUCCESS) */
  /*     printf("n3\n"); */
  /*   ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk */
  /*   if(ret != CL_SUCCESS) */
  /*     printf("fblk\n"); */
      
  /*   // Execute the OpenCL kernel on the list */
  /*   size_t global_item_size = 1024*2; // Process the entire lists //number of work groups */
  /*   //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length */
  /*   size_t local_item_size = 1024; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024 */
  /*   ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL,  */
  /* 				 &global_item_size, &local_item_size, 0, NULL, NULL); */
  /*   if(ret != CL_SUCCESS){ */
  /*     printf("well that failed\n"); */
  /*     if(ret == CL_INVALID_PROGRAM_EXECUTABLE) printf("invalid program executable\n"); */
  /*     if(ret == CL_INVALID_COMMAND_QUEUE) printf("invalid cq\n"); */
  /*     if(ret == CL_INVALID_KERNEL) printf("kernel\n"); */
  /*     if(ret == CL_INVALID_CONTEXT) printf("context\n"); */
  /*     if(ret == CL_INVALID_KERNEL_ARGS) printf("args\n"); */
  /*     if(ret == CL_INVALID_WORK_DIMENSION) printf("word d\n"); */
  /*     if(ret == CL_INVALID_GLOBAL_WORK_SIZE) printf("gwerksz\n"); */
  /*     if(ret == CL_INVALID_WORK_GROUP_SIZE) printf("grpsz\n"); */
  /*     if(ret == CL_INVALID_EVENT_WAIT_LIST) printf("waitlist\n"); */
  /*     if(ret == CL_OUT_OF_HOST_MEMORY) printf("outofmem\n"); */
  /*   } */
  /*   //get output */
  /*   unsigned char *O2 = (unsigned char*)malloc((sizeof(char)*BLKSIZE)); */
  /*   unsigned char *O3 = (unsigned char*)malloc((sizeof(char)*BLKSIZE)); */
  /*   int N2; */
  /*   int N3; */
  /*   unsigned short sh; */
  /*   ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 			      sizeof(int), &N2, 0, NULL, NULL); */
  /*    if(ret != CL_SUCCESS)  */
  /*      printf("not lx  successful\n");  */
  /*   /\* if(ret == CL_INVALID_COMMAND_QUEUE) printf("CL_INVALID_COMMAND_QUEUE\n"); *\/ */
  /*   /\* if(ret == CL_INVALID_CONTEXT) printf("CL_INVALID_CONTEXT\n"); *\/ */
  /*   /\* if(ret == CL_INVALID_MEM_OBJECT) printf("CL_INVALID_MEM_OBJECT\n"); *\/ */
  /*   /\* if(ret == CL_INVALID_VALUE) printf("CL_INVALID_VALUE\n"); *\/ */
  /*   /\* if(ret == CL_INVALID_EVENT_WAIT_LIST) printf("CL_INVALID_EVENT_WAIT_LIST\n"); *\/ */
  /*   /\* if(ret == CL_MISALIGNED_SUB_BUFFER_OFFSET) printf("CL_MISALIGNED_SUB_BUFFER_OFFSET\n"); *\/ */
  /*   /\* if(ret == CL_DEVICE_MEM_BASE_ADDR_ALIGN) printf("CL_DEVICE_MEM_BASE_ADDR_ALIGN\n"); *\/ */
  /*   /\* if(ret == CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST) printf("CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST\n"); *\/ */
  /*   /\* if(ret == CL_MEM_OBJECT_ALLOCATION_FAILURE) printf("CL_MEM_OBJECT_ALLOCATION_FAILURE\n"); *\/ */
  /*   /\* if(ret == CL_OUT_OF_RESOURCES) printf("CL_OUT_OF_RESOURCES\n"); *\/ */
  /*   /\* if(ret == CL_OUT_OF_HOST_MEMORY) printf("CL_OUT_OF_HOST_MEMORY\n"); *\/ */
  /*   ret = clEnqueueReadBuffer(command_queue, n3_mem_obj, CL_TRUE, 0, */
  /* 			      sizeof(int), &N3, 0, NULL, NULL); */
  /*   ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0, */
  /* 			      BLKSIZE * sizeof(char), O2, 0, NULL, NULL); */
  /*   ret = clEnqueueReadBuffer(command_queue, o3_mem_obj, CL_TRUE, 0, */
  /* 			      BLKSIZE * sizeof(char), O3, 0, NULL, NULL); */
  /*   ret = clEnqueueReadBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 			       sizeof(short), &sh, 0, NULL, NULL); */
    
  /*   printf("sh %x\n",sh); */
  /*   //this loop will write the proper bits to the N2 array and shift the N3 array the correct amount */
  /*   if(((sh>>8)&0x0f) != 0){ //only do this if a shift is necessary */
  /*     pshiftAmnt = (sh>>8) & 0x0f;//sh>>8 & 0x0f is the shift amount for O2 */
  /*     printf("pshift %x\n",pshiftAmnt); */
  /*     unsigned char dc = O2[N2]; */
  /*     unsigned char db = O2[N2-1]; */
  /*     unsigned char da = O3[0]; */
  /*     unsigned char dd = O3[1]; */
  /*     printf("o2n2-1: %x\n02n2: %x\n",db,dc); */
  /*     printf("o30: %x\n031: %x\n",da,dd); */
  /*     O2[N2-1] |= (O3[0]<<pshiftAmnt);//because O2 is the first block */
  /*     dc = O2[N2-1]; */
  /*     printf("02n2: %x\n",dc); */
  /*     O3[0] >>= 8 - pshiftAmnt; */
  /*     for(int i = 1;i<N3+1;i++){//this will be shifting all of O3 */
  /* 	O3[i-1] |= O3[i]<<pshiftAmnt; */
  /* 	O3[i] >>= 8 - pshiftAmnt; */
  /*     } */
  /*     //pbyte = O3[N3]>>pshiftAmnt; */
  /*     da = O3[0]; */
  /*     dd = O3[1]; */
  /*     printf("o30: %x\n031: %x\n",da,dd); */
  /*   } */
  /*   //pshiftAmnt = sh & 0x0f; */
    
    
  /*   printf("n2 %d n3 %d\n",N2,N3); */
  /*   //write output to file */
    

  /*   fwrite(O2,sizeof(char),N2,write_ptr); //write to file */
  /*   //fwrite(O3,sizeof(char),N3,write_ptr); //write to file */

  /*   //cleanup */
  /*   ret = clReleaseKernel(kernel); */
  /*   ret = clReleaseProgram(program); */
  /*   free(O2); */
  /*   free(O3); */
  /* }else {//fblk = 5 */
  /*   fblk = 5; */
  /*   Nblks-=2; */
  /*   //more blocks after */
  /*   //copy data to memory */
  /*   fread(buffer,sizeof(buffer),1,fptr); */
  /*   ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0, */
  /* 			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL); */
  /*   fread(buffer,sizeof(buffer),1,fptr); */
  /*   ret = clEnqueueWriteBuffer(command_queue, b3_mem_obj, CL_TRUE, 0, */
  /* 			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL); */
  /*   //n2 */
  /*   ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 			       sizeof(int), &blksize, 0, NULL, NULL); */
  /*   //n3 */
  /*   ret = clEnqueueWriteBuffer(command_queue, n3_mem_obj, CL_TRUE, 0, */
  /* 			       sizeof(int), &blksize, 0, NULL, NULL); */
  /*   //fblk */
  /*   ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 			       sizeof(short), &fblk, 0, NULL, NULL); */
  /*   //run program */
  /*   // Create a program from the kernel source */
  /*   cl_program program = clCreateProgramWithSource(context, 1,  */
  /* 						   (const char **)&source_str, */
  /* 						   (const size_t *)&source_size, &ret); */

  /*   // Build the program */
  /*   ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL); */

  /*   //create the kernel */
  /*   cl_kernel kernel = clCreateKernel(program,"lz77",&ret); */

  /*   //set the args of the kernel */
  /*   ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1 */
  /*   ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2 */
  /*   ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3 */
  /*   ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2 */
  /*   ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3 */
  /*   ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1 */
  /*   ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2 */
  /*   ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3 */
  /*   ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk */

  /*   // Execute the OpenCL kernel on the list */
  /*   size_t global_item_size = 1024*2; // Process the entire lists //number of work groups */
  /*   //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length */
  /*   size_t local_item_size = 1024; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024 */
  /*   ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL,  */
  /* 				 &global_item_size, &local_item_size, 0, NULL, NULL); */
  /*   //get output */
  /*   char *O2 = (char*)malloc((sizeof(char)*BLKSIZE)); */
  /*   char *O3 = (char*)malloc((sizeof(char)*BLKSIZE)); */
  /*   int N2; */
  /*   int N3; */
  /*   unsigned short sh; */
  /*   ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 			      sizeof(int), &N2, 0, NULL, NULL); */
  /*   ret = clEnqueueReadBuffer(command_queue, n3_mem_obj, CL_TRUE, 0, */
  /* 			      sizeof(int), &N3, 0, NULL, NULL); */
  /*   ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0, */
  /* 			      BLKSIZE * sizeof(char), O2, 0, NULL, NULL); */
  /*   ret = clEnqueueReadBuffer(command_queue, o3_mem_obj, CL_TRUE, 0, */
  /* 			      BLKSIZE * sizeof(char), O3, 0, NULL, NULL); */
  /*   ret = clEnqueueReadBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 			       sizeof(short), &sh, 0, NULL, NULL); */


  /*   printf("sh %x\n",sh); */
  /*   //this loop will write the proper bits to the N2 array and shift the N3 array the correct amount */
  /*   if(((sh>>8)&0x0f) != 0){ //only do this if a shift is necessary */
  /*     pshiftAmnt = (sh>>8) & 0x0f;//sh>>8 is the shift amount for O2 */
  /*     printf("pshift %x\n",pshiftAmnt); */
  /*     unsigned char dc = O2[N2]; */
  /*     unsigned char db = O2[N2-1]; */
  /*     unsigned char da = O3[0]; */
  /*     unsigned char dd = O3[1]; */
  /*     printf("o2n2-1: %x\n02n2: %x\n",db,dc); */
  /*     printf("o30: %x\n031: %x\n",da,dd); */
  /*     O2[N2+1] |= (O3[0]<<pshiftAmnt);//because O2 is the first block */
  /*     dc = O2[N2]; */
  /*     printf("02n2: %x\n",dc); */
  /*     O3[0] >>= 8 - pshiftAmnt; */
  /*     O3[N3+2] = 0; */
  /*     for(int i = 1;i<N3+2;i++){//this will be shifting all of O3 */
  /* 	O3[i-1] |= O3[i]<<pshiftAmnt; */
  /* 	O3[i] >>= 8 - pshiftAmnt; */
  /*     } */
  /*     if((pshiftAmnt + (sh>>12))>=8){ */
  /* 	pbyte = O3[N3]; */
  /* 	N3--; */
  /* 	pshiftAmnt = (pshiftAmnt + (sh>>12))%8; */
  /*     }else{ */
  /* 	pbyte = O3[N3+1]; */
  /* 	pshiftAmnt += (sh>>12); */
  /*     } */
      
  /*     da = O3[0]; */
  /*     dd = O3[1]; */
  /*     printf("o30: %x\n031: %x\n",da,dd); */
  /*   } */

    
  /*   //write output to file */
  /*   fwrite(O2,sizeof(char),N2+2,write_ptr); //write to file */
  /*   fwrite(O3,sizeof(char),N3+1,write_ptr); //write to file */
    
  /*   //set b1 to b3 */
  /*   ret = clEnqueueWriteBuffer(command_queue, b1_mem_obj, CL_TRUE, 0, */
  /* 			       BLKSIZE * sizeof(char), buffer, 0, NULL, NULL); */
  /*   //cleanup */
  /*   ret = clReleaseKernel(kernel); */
  /*   ret = clReleaseProgram(program); */
  /*   free(O2); */
  /*   free(O3); */
  /* } */
  
  /* while(running){ */
  /*   if(Nblks > 2){ //fblk = 6 */
  /*     fblk = 6; */
  /*     Nblks-=2; */
  /*     //copy data to memory */
  /*     fread(buffer,sizeof(buffer),1,fptr); */
  /*     ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0, */
  /* 				 BLKSIZE * sizeof(char), buffer, 0, NULL, NULL); */
  /*     fread(buffer,sizeof(buffer),1,fptr); */
  /*     ret = clEnqueueWriteBuffer(command_queue, b3_mem_obj, CL_TRUE, 0, */
  /* 				 BLKSIZE * sizeof(char), buffer, 0, NULL, NULL); */
  /*     //n1 */
  /*     ret = clEnqueueWriteBuffer(command_queue, n1_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(int), &blksize, 0, NULL, NULL); */
  /*     //n2 */
  /*     ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(int), &blksize, 0, NULL, NULL); */
  /*     //n3 */
  /*     ret = clEnqueueWriteBuffer(command_queue, n3_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(int), &blksize, 0, NULL, NULL); */
  /*     //fblk */
  /*     ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(short), &fblk, 0, NULL, NULL); */
  /*     //run program */
  /*     // Create a program from the kernel source */
  /*     cl_program program = clCreateProgramWithSource(context, 1,  */
  /* 						     (const char **)&source_str, */
  /* 						     (const size_t *)&source_size, &ret); */

  /*     // Build the program */
  /*     ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL); */

  /*     //create the kernel */
  /*     cl_kernel kernel = clCreateKernel(program,"lz77",&ret); */

  /*     //set the args of the kernel */
  /*     ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1 */
  /*     ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2 */
  /*     ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3 */
  /*     ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2 */
  /*     ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3 */
  /*     ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1 */
  /*     ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2 */
  /*     ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3 */
  /*     ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk */

  /*     // Execute the OpenCL kernel on the list */
  /*     size_t global_item_size = 1024*2; // Process the entire lists //number of work groups */
  /*     //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length */
  /*     size_t local_item_size = 1024; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024 */
  /*     ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL,  */
  /* 				   &global_item_size, &local_item_size, 0, NULL, NULL); */
  /*     //get output */
  /*     char *O2 = (char*)malloc((sizeof(char)*BLKSIZE)); */
  /*     char *O3 = (char*)malloc((sizeof(char)*BLKSIZE)); */
  /*     int N2; */
  /*     int N3; */
  /*     unsigned short sh; */
  /*     ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 				sizeof(int), &N2, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, n3_mem_obj, CL_TRUE, 0, */
  /* 				sizeof(int), &N3, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0, */
  /* 				BLKSIZE * sizeof(char), O2, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, o3_mem_obj, CL_TRUE, 0, */
  /* 				BLKSIZE * sizeof(char), O3, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 				sizeof(short), &sh, 0, NULL, NULL); */


  /*     printf("sh %x\n",sh); */
  /*     //this loop will write the proper bits to the N2 array and shift the N3 array the correct amount */
  /*     if(pshiftAmnt !=0){ */
  /* 	pbyte |= (O2[0]<<pshiftAmnt);//because O2 is the first block */


  /* 	O2[0] >>= 8 - pshiftAmnt; */
  /* 	O2[N2+2] = 0; */
  /* 	for(int i = 1;i<N2+2;i++){//this will be shifting all of O3 */
  /* 	  O2[i-1] |= O2[i]<<pshiftAmnt; */
  /* 	  O2[i] >>= 8 - pshiftAmnt; */
  /* 	} */
  /* 	if((pshiftAmnt + ((sh>>8) & 0x0f))>=8){ */
  /* 	  //pbyte = O3[N3]; */
  /* 	  N2--; */
  /* 	  pshiftAmnt = (pshiftAmnt + ((sh>>8) & 0x0f))%8; */
  /* 	}else{ */
  /* 	  //pbyte = O3[N3+1]; */
  /* 	  pshiftAmnt += ((sh>>8) & 0x0f); */
  /* 	} */
  /*     } */
  /*     //write out pbyte */
  /*     fwrite(&pbyte,sizeof(char),1,write_ptr); */
  /*     if(pshiftAmnt != 0){ //only do this if a shift is necessary */
  /* 	//pshiftAmnt = (sh>>8) & 0x0f;//sh>>8 is the shift amount for O2 */
  /* 	printf("pshift %x\n",pshiftAmnt); */
  /* 	unsigned char dc = O2[N2]; */
  /* 	unsigned char db = O2[N2-1]; */
  /* 	unsigned char da = O3[0]; */
  /* 	unsigned char dd = O3[1]; */
  /* 	printf("o2n2-1: %x\n02n2: %x\n",db,dc); */
  /* 	printf("o30: %x\n031: %x\n",da,dd); */
  /* 	O2[N2+1] |= (O3[0]<<pshiftAmnt);//because O2 is the first block */
  /* 	dc = O2[N2]; */
  /* 	printf("02n2: %x\n",dc); */
  /* 	O3[0] >>= 8 - pshiftAmnt; */
  /* 	O3[N3+2] = 0; */
  /* 	for(int i = 1;i<N3+2;i++){//this will be shifting all of O3 */
  /* 	  O3[i-1] |= O3[i]<<pshiftAmnt; */
  /* 	  O3[i] >>= 8 - pshiftAmnt; */
  /* 	} */
  /* 	if((pshiftAmnt + (sh>>12))>8){ */
  /* 	  pbyte = O3[N3]; */
  /* 	  N3--; */
  /* 	  pshiftAmnt = (pshiftAmnt + (sh>>12))%8; */
  /* 	}else{ */
  /* 	  pbyte = O3[N3+1]; */
  /* 	  pshiftAmnt += (sh>>12); */
  /* 	} */
      
  /* 	da = O3[0]; */
  /* 	dd = O3[1]; */
  /* 	printf("o30: %x\n031: %x\n",da,dd); */
  /*     } */
  /*     //write output to file */
  /*     fwrite(O2,sizeof(char),N2+2,write_ptr); //write to file */
  /*     fwrite(O3,sizeof(char),N3,write_ptr); //write to file */
    
  /*     //set b1 to b3 */
  /*     ret = clEnqueueWriteBuffer(command_queue, b1_mem_obj, CL_TRUE, 0, */
  /* 				 BLKSIZE * sizeof(char), buffer, 0, NULL, NULL); */
  /*     running = 0; */
  /*     //cleanup */
  /*     ret = clReleaseKernel(kernel); */
  /*     ret = clReleaseProgram(program); */
  /*     free(O2); */
  /*     free(O3); */
  /*   }else if( Nblks == 1 ) { //fblk = 4 */
  /*     fblk = 4; */
  /*     //copy data to memory */
  /*     fread(buffer,finalBytes * sizeof(char),1,fptr); */
  /*     ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0, */
  /* 				 finalBytes * sizeof(char), buffer, 0, NULL, NULL); */
  /*     //n1 */
  /*     ret = clEnqueueWriteBuffer(command_queue, n1_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(int), &blksize, 0, NULL, NULL); */
  /*     //n2 */
  /*     ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(int), &finalBytes, 0, NULL, NULL); */
  /*     //fblk */
  /*     ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(short), &fblk, 0, NULL, NULL); */
  /*     //run program */
  /*     // Create a program from the kernel source */
  /*     cl_program program = clCreateProgramWithSource(context, 1,  */
  /* 						     (const char **)&source_str, */
  /* 						     (const size_t *)&source_size, &ret); */

  /*     // Build the program */
  /*     ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL); */

  /*     //create the kernel */
  /*     cl_kernel kernel = clCreateKernel(program,"lz77",&ret); */

  /*     //set the args of the kernel */
  /*     ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1 */
  /*     ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2 */
  /*     ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3 */
  /*     ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2 */
  /*     ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3 */
  /*     ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1 */
  /*     ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2 */
  /*     ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3 */
  /*     ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk */

  /*     // Execute the OpenCL kernel on the list */
  /*     size_t global_item_size = 1024*2; // Process the entire lists //number of work groups */
  /*     //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length */
  /*     size_t local_item_size = 1024; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024 */
  /*     ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL,  */
  /* 				   &global_item_size, &local_item_size, 0, NULL, NULL); */
  /*     //get output */
  /*     char *O2 = (char*)malloc((sizeof(char)*BLKSIZE)); */
  /*     int N2; */
  /*     unsigned short sh; */
  /*     ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 				sizeof(int), &N2, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0, */
  /* 				N2 * sizeof(char), O2, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 				sizeof(short), &sh, 0, NULL, NULL); */


  /*     printf("sh %x\n",sh); */
  /*     //this loop will write the proper bits to the N2 array and shift the N3 array the correct amount */
  /*     if(pshiftAmnt !=0){ */
  /* 	pbyte |= (O2[0]<<pshiftAmnt);//because O2 is the first block */

  /* 	O2[0] >>= 8 - pshiftAmnt; */
  /* 	O2[N2+2] = 0; */
  /* 	for(int i = 1;i<N2+2;i++){//this will be shifting all of O3 */
  /* 	  O2[i-1] |= O2[i]<<pshiftAmnt; */
  /* 	  O2[i] >>= 8 - pshiftAmnt; */
  /* 	} */
  /* 	if((pshiftAmnt + ((sh>>8) & 0x0f))>=8){ */
  /* 	  //pbyte = O3[N3]; */
  /* 	  N2--; */
  /* 	  pshiftAmnt = (pshiftAmnt + ((sh>>8) & 0x0f))%8; */
  /* 	}else{ */
  /* 	  //pbyte = O3[N3+1]; */
  /* 	  pshiftAmnt += ((sh>>8) & 0x0f); */
  /* 	} */
  /*     } */
  /*     //write out pbyte */
  /*     fwrite(&pbyte,sizeof(char),1,write_ptr); */
    
  /*     //write output to file */
  /*     fwrite(O2,sizeof(char),N2+2,write_ptr); //write to file */

  /*     running = 0; */
  /*     //cleanup */
  /*     ret = clReleaseKernel(kernel); */
  /*     ret = clReleaseProgram(program); */
  /*     free(O2); */
  /*   } else { //fblk = 3 */
  /*     fblk = 3; */
  /*     //copy data to memory */
  /*     fread(buffer,sizeof(buffer),1,fptr); */
  /*     ret = clEnqueueWriteBuffer(command_queue, b2_mem_obj, CL_TRUE, 0, */
  /* 				 BLKSIZE * sizeof(char), buffer, 0, NULL, NULL); */
  /*     fread(buffer,finalBytes * sizeof(char),1,fptr); */
  /*     ret = clEnqueueWriteBuffer(command_queue, b3_mem_obj, CL_TRUE, 0, */
  /* 				 finalBytes * sizeof(char), buffer, 0, NULL, NULL); */
  /*     //n1 */
  /*     ret = clEnqueueWriteBuffer(command_queue, n1_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(int), &blksize, 0, NULL, NULL); */
  /*     //n2 */
  /*     ret = clEnqueueWriteBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(int), &blksize, 0, NULL, NULL); */
  /*     //n3 */
  /*     ret = clEnqueueWriteBuffer(command_queue, n3_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(int), &finalBytes, 0, NULL, NULL); */
  /*     //fblk */
  /*     ret = clEnqueueWriteBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 				 sizeof(short), &fblk, 0, NULL, NULL); */
  /*     //run program */
  /*     // Create a program from the kernel source */
  /*     cl_program program = clCreateProgramWithSource(context, 1,  */
  /* 						     (const char **)&source_str, */
  /* 						     (const size_t *)&source_size, &ret); */

  /*     // Build the program */
  /*     ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL); */

  /*     //create the kernel */
  /*     cl_kernel kernel = clCreateKernel(program,"lz77",&ret); */

  /*     //set the args of the kernel */
  /*     ret = clSetKernelArg(kernel,0,sizeof(cl_mem),(void *)&b1_mem_obj);//b1 */
  /*     ret = clSetKernelArg(kernel,1,sizeof(cl_mem),(void *)&b2_mem_obj);//b2 */
  /*     ret = clSetKernelArg(kernel,2,sizeof(cl_mem),(void *)&b3_mem_obj);//b3 */
  /*     ret = clSetKernelArg(kernel,3,sizeof(cl_mem),(void *)&o2_mem_obj);//o2 */
  /*     ret = clSetKernelArg(kernel,4,sizeof(cl_mem),(void *)&o3_mem_obj);//o3 */
  /*     ret = clSetKernelArg(kernel,5,sizeof(cl_mem),(void *)&n1_mem_obj);//n1 */
  /*     ret = clSetKernelArg(kernel,6,sizeof(cl_mem),(void *)&n2_mem_obj);//n2 */
  /*     ret = clSetKernelArg(kernel,7,sizeof(cl_mem),(void *)&n3_mem_obj);//n3 */
  /*     ret = clSetKernelArg(kernel,8,sizeof(cl_mem),(void *)&fblk_mem_obj);//fblk */

  /*     // Execute the OpenCL kernel on the list */
  /*     size_t global_item_size = 1024*2; // Process the entire lists //number of work groups */
  /*     //local cannot be bigger than global (duh) maybe it needs to be a divisor of the length */
  /*     size_t local_item_size = 1024; // Process in groups of 64 //number of threads per work group //use 32k as this or 1024 */
  /*     ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL,  */
  /* 				   &global_item_size, &local_item_size, 0, NULL, NULL); */
  /*     //get output */
  /*     char *O2 = (char*)malloc((sizeof(char)*BLKSIZE)); */
  /*     char *O3 = (char*)malloc((sizeof(char)*BLKSIZE)); */
  /*     int N2; */
  /*     int N3; */
  /*     unsigned short sh; */
  /*     ret = clEnqueueReadBuffer(command_queue, n2_mem_obj, CL_TRUE, 0, */
  /* 				sizeof(int), &N2, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, n3_mem_obj, CL_TRUE, 0, */
  /* 				sizeof(int), &N3, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, o2_mem_obj, CL_TRUE, 0, */
  /* 				BLKSIZE * sizeof(char), O2, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, o3_mem_obj, CL_TRUE, 0, */
  /* 				BLKSIZE * sizeof(char), O3, 0, NULL, NULL); */
  /*     ret = clEnqueueReadBuffer(command_queue, fblk_mem_obj, CL_TRUE, 0, */
  /* 				sizeof(short), &sh, 0, NULL, NULL); */


  /*     printf("sh %x\n",sh); */
  /*     //this loop will write the proper bits to the N2 array and shift the N3 array the correct amount */
  /*     if(pshiftAmnt !=0){ */
  /* 	pbyte |= (O2[0]<<pshiftAmnt);//because O2 is the first block */

  /* 	O2[0] >>= 8 - pshiftAmnt; */
  /* 	O2[N2+2] = 0; */
  /* 	for(int i = 1;i<N2+2;i++){//this will be shifting all of O3 */
  /* 	  O2[i-1] |= O2[i]<<pshiftAmnt; */
  /* 	  O2[i] >>= 8 - pshiftAmnt; */
  /* 	} */
  /* 	if((pshiftAmnt + ((sh>>8) & 0x0f))>=8){ */
  /* 	  //pbyte = O3[N3]; */
  /* 	  N2--; */
  /* 	  pshiftAmnt = (pshiftAmnt + ((sh>>8) & 0x0f))%8; */
  /* 	}else{ */
  /* 	  //pbyte = O3[N3+1]; */
  /* 	  pshiftAmnt += ((sh>>8) & 0x0f); */
  /* 	} */
  /*     } */
  /*     //write out pbyte */
  /*     //fwrite(&pbyte,sizeof(char),1,write_ptr); */
  /*     if(pshiftAmnt != 0){ //only do this if a shift is necessary */
  /* 	//pshiftAmnt = (sh>>8) & 0x0f;//sh>>8 is the shift amount for O2 */
  /* 	printf("pshift %x\n",pshiftAmnt); */
  /* 	unsigned char dc = O2[N2]; */
  /* 	unsigned char db = O2[N2-1]; */
  /* 	unsigned char da = O3[0]; */
  /* 	unsigned char dd = O3[1]; */
  /* 	printf("o2n2-1: %x\n02n2: %x\n",db,dc); */
  /* 	printf("o30: %x\n031: %x\n",da,dd); */
  /* 	O2[N2+1] |= (O3[0]<<pshiftAmnt);//because O2 is the first block */
  /* 	dc = O2[N2]; */
  /* 	printf("02n2: %x\n",dc); */
  /* 	O3[0] >>= 8 - pshiftAmnt; */
  /* 	O3[N3+2] = 0; */
  /* 	for(int i = 1;i<N3+2;i++){//this will be shifting all of O3 */
  /* 	  O3[i-1] |= O3[i]<<pshiftAmnt; */
  /* 	  O3[i] >>= 8 - pshiftAmnt; */
  /* 	} */
  /* 	if((pshiftAmnt + (sh>>12))>8){ */
  /* 	  pbyte = O3[N3]; */
  /* 	  N3--; */
  /* 	  pshiftAmnt = (pshiftAmnt + (sh>>12))%8; */
  /* 	}else{ */
  /* 	  pbyte = O3[N3+1]; */
  /* 	  pshiftAmnt += (sh>>12); */
  /* 	} */
      
  /* 	da = O3[0]; */
  /* 	dd = O3[1]; */
  /* 	printf("o30: %x\n031: %x\n",da,dd); */
  /*     } */
      
  /*     //write output to file */
  /*     //fwrite(O2,sizeof(char),N2+2,write_ptr); //write to file */
  /*     //fwrite(O3,sizeof(char),N3+1,write_ptr); //write to file */
    
  /*     running = 0; //this is the end */
  /*     //cleanup */
  /*     ret = clReleaseKernel(kernel); */
  /*     ret = clReleaseProgram(program); */
  /*     free(O2); */
  /*     free(O3); */
  /*   } */
  /* } */
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

