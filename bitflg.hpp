#pragma once
namespace BitFlags {
	/*
	enum{
		option1 = 0x01, //decimal: 1 hex: 0x1 binary: 0000 0001
		option2 = 0x02, //decimal: 2 hex: 0x2 binary: 0000 0010
		option3 = 0x04, //decimal: 4 hex: 0x4 binary: 0000 0100
		option4 = 0x08, //decimal: 8 hex: 0x8 binary: 0000 1000
		option5 = 0x10, //decimal: 16 hex: 0x10 binary:  0000 0001 0000
		option6 = 0x20, //decimal: 32 hex: 0x20 binary:  0000 0010 0000
		option7 = 0x40, //decimal: 64 hex: 0x40 binary:  0000 0100 0000
		option8 = 0x80, //decimal: 128 hex: 0x80 binary: 0000 1000 0000
		option9 = 0x100, //decimal: 100 hex: 0x100 binary: 0001 0000 0000
		option10 = 0x200 //decimal: 512 hex: 0x200 binary: 0010 0000 0000
	};
	*/
	enum {
		option1 = 1,
		option2 = 1 << 1,
		option3 = 1 << 2,
		option4 = 1 << 3,
		option5 = 1 << 4,
		option6 = 1 << 5,
		option7 = 1 << 6,
		option8 = 1 << 7,
		option9 = 1 << 8,
		option10 = 1 << 9
	};

	int _inline make_flag(int index) { return 1 << index; };
};