
eir_sources = files(
	'../../frigg/src/debug.cpp',
	'../../frigg/src/libc.cpp',
	'../../frigg/src/arch_x86/gdt.cpp',
	'../common/font-8x16.cpp',
	'main.cpp',
	'multiboot.S')

executable('eir', eir_sources,
	include_directories: include_directories(
		'../include/',
		'../common/',
		'../../frigg/include'),
	c_args: ['-std=c11', '-m32', '-mcmodel=32'],
	cpp_args: ['-std=c++17', '-m32', '-mcmodel=32',
		'-DFRIGG_NO_LIBC',
		'-Wall'],
	link_args: ['-m32', '-nostdlib',
		'-T', meson.current_source_dir() + '/link.x'],
	objects: './libgcc.a',
	link_depends: files('link.x'),
	install: true)

