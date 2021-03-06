set (TCTFB_SRCS video.cpp ../utils.cpp)
set (TCTFB_CUDA_SRCS ${TCTFB_SRCS} ../cuda/cuda_kernel.cu ../cuda/time_interval.cu)

include_directories (
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/atomic
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../cuda
  ${CMAKE_CURRENT_SOURCE_DIR}/../cuda/cuda_utils
  )

set( OpenCV_DIR /opt/local/lib/cmake )
FIND_PACKAGE( OpenCV REQUIRED)

if(OpenCV_FOUND)
  add_executable (tctfb_ff ${TCTFB_SRCS})
  set_target_properties (tctfb_ff PROPERTIES COMPILE_DEFINITIONS "TIME;SEQ_DETECTION")
  target_link_libraries (tctfb_ff ${CMAKE_THREAD_LIBS_INIT})
  TARGET_LINK_LIBRARIES( tctfb_ff ${OpenCV_LIBS} )



  #CUDA variants
  #allowed flags: CUDA_PINNED_MEMORY
  FIND_PACKAGE(CUDA)

  if(CUDA_FOUND)
    link_directories("/usr/local/cuda/lib")
    include(FindCUDA)
    set(CUDA_NVCC_FLAGS "--use_fast_math")
  
    cuda_add_executable ( tctfb_cuda ${TCTFB_CUDA_SRCS} )
    target_link_libraries ( tctfb_cuda "cudart" )
    target_link_libraries( tctfb_cuda ${OpenCV_LIBS} )
    set_target_properties ( tctfb_cuda PROPERTIES COMPILE_DEFINITIONS "CUDA;TIME;AVG_TERMINATION" )
    set_target_properties ( tctfb_cuda PROPERTIES COMPILE_FLAGS "-Wall -O6" )

  else(CUDA_FOUND)
    message (WARNING "Cuda not found: can't configure CUDA-enabled tctfb (video denoiser)")
  endif(CUDA_FOUND)
else(OpenCV_FOUND)
  message (WARNING "OpenCV not found: can't configure tctfb (video denoiser)")
endif(OpenCV_FOUND)
