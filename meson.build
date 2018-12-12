project('mono-ssp', 'cpp',
  version : '0.1.1',
  default_options : [
    'warning_level=3',
    'cpp_std=c++17'
  ]
)

dep_unwind = dependency('libunwind')
dep_unwind_ptrace = dependency('libunwind-ptrace')
dep_threads = dependency('threads') # -lpthread

exe = executable(
  'mono_ssp',
  [
    'mono_ssp.cpp',
    'backtrace.cpp',
    'sample_process.cpp',
    'fast_sample.cpp'
  ],
  install : true,
  dependencies: [
    dep_unwind,
    dep_unwind_ptrace,
    dep_threads
  ]
)

test('basic', exe)
