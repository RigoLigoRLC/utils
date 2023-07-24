
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <inttypes.h>
#include <string.h>

using namespace std;
#define TOSTR reinterpret_cast<char*>


inline uint32_t PaToPfn_PAE(uint64_t pa) { return (pa & 0xFFFFFF000) >> 12; }
inline uint32_t PageBase_PAE(uint64_t pa) { return pa & 0xFFFFFF000; }

#define TLBDBG
#ifdef TLBDBG
#define TLBTRACE(fmt, ...) printf(fmt, __VA_ARGS__)
#else
#define TLBTRACE(...)
#endif

union VirtAddr { uint32_t Addr; struct __attribute__((packed)) { uint32_t offset:12; uint32_t PTI:9; uint32_t PDI:9; uint32_t PDPI:2; } VA; };
struct TLB_PAE {
	struct _sPDPT {
		_sPDPT() { BasePA = ULLONG_MAX; memset(sPDPTE, 0, sizeof(sPDPT)); }
		~_sPDPT() { for(auto i : sPDPTE) if(i) delete i; }
		uint64_t BasePA;
		struct _sPDPTE_PAE {
			_sPDPTE_PAE() { BasePA = ULLONG_MAX; memset(sPDE, 0, sizeof(sPDE)); }
			~_sPDPTE_PAE() { for(auto i : sPDE) if(i) delete i; }
			uint64_t BasePA;
			struct _sPDE_PAE {
				_sPDE_PAE() { BasePA = ULLONG_MAX; LargePage = false; memset(Pte, 0, sizeof(Pte)); }
				uint64_t BasePA;
				bool LargePage;
				uint64_t Pte[512];
			} *sPDE[512];
		} *sPDPTE[4];
	} sPDPT;
};
struct TLB {
	struct _sPDE_PAE {
		_sPDE_PAE() { BasePA = ULLONG_MAX; memset(Pte, 0, sizeof(Pte)); }
		uint64_t BasePA;
		uint64_t Pte[512];
	} *sPDE[512];
};

struct DumpContext {
	ifstream File;
	size_t PagesOffset = 0;
	uint8_t *PagesBitmap = nullptr;
	bool PAE = false;
	TLB_PAE TLBpae;
	TLB TLBnopae;
};

template<typename T>
inline bool ReadPhysicalAddress(DumpContext& ctx, uint64_t paddr, T& data)
{
	uint32_t PFN = paddr >> 12;
	if(!(ctx.PagesBitmap[PFN / 8] & (1 << (PFN % 8)))) return false; // Check bitmap
	uint32_t pageIndex = 0;
	// Accumulate the populated bits to find out which page is it
	for(uint32_t i = 0; i < PFN / 8; i++) pageIndex += __builtin_popcount((unsigned int)ctx.PagesBitmap[i]);
	pageIndex += __builtin_popcount((unsigned int)(ctx.PagesBitmap[PFN / 8] & (0xFF >> (7 - PFN % 8))));
	// Seek and read value
	ctx.File.seekg(ctx.PagesOffset + 0x1000 * (pageIndex - 1) + (paddr & 0xFFF));
	ctx.File.read(TOSTR(&data), sizeof(T));
	return true;
}

uint64_t PopulateTlb(DumpContext& ctx, uint32_t cr3)
{
	uint64_t PteCount = 0;
	
	TLB_PAE& ret = ctx.TLBpae;
	// PDPT
	ret.sPDPT.BasePA = cr3;
	// PDPTE
	for(int ii = 0; ii < 4; ii++) {
		uint64_t PDPTE; ReadPhysicalAddress(ctx, uint64_t(cr3 + ii * 8), PDPTE);
		if(!PaToPfn_PAE(PDPTE)) continue;
		TLBTRACE("PDPTE #%d @ %X ==> %X\n", ii, cr3 + ii * 8, PageBase_PAE(PDPTE));
		ret.sPDPT.sPDPTE[ii] = new TLB_PAE::_sPDPT::_sPDPTE_PAE;
		ret.sPDPT.sPDPTE[ii]->BasePA = PageBase_PAE(PDPTE);
		// PDE
		for(int jj = 0; jj < 512; jj++) {
			uint64_t pde; ReadPhysicalAddress(ctx, PageBase_PAE(PDPTE) + jj * 8, pde);
			if(!(pde & 1)) continue;
			ret.sPDPT.sPDPTE[ii]->sPDE[jj] = new TLB_PAE::_sPDPT::_sPDPTE_PAE::_sPDE_PAE;
			ret.sPDPT.sPDPTE[ii]->sPDE[jj]->BasePA = PageBase_PAE(PDPTE) + jj * 8;
			if(pde & 0x80) { // Large page, 2MiB on PAE system
				TLBTRACE(" -- PDE #%d @ %X [LARGE]==> %X\n", jj, PageBase_PAE(PDPTE) + jj * 8, PageBase_PAE(pde));
				ret.sPDPT.sPDPTE[ii]->sPDE[jj]->LargePage = true;
				ret.sPDPT.sPDPTE[ii]->sPDE[jj]->Pte[0] = (pde & 0xFFFE00000); // This is the PA of large page
				continue;
			}
			TLBTRACE(" -- PDE #%d @ %X ==> %X\n", jj, PageBase_PAE(PDPTE) + jj * 8, PageBase_PAE(pde));
			// PTE
			for(int kk = 0; kk < 512; kk++) {
				uint64_t pte; ReadPhysicalAddress(ctx, PageBase_PAE(pde) + kk * 8, pte);
				if(!(pte & 1)) continue;
				ret.sPDPT.sPDPTE[ii]->sPDE[jj]->Pte[kk] = pte;
				PteCount++;
			}
		}
	}
	
	return PteCount;
}

inline uint64_t VaToPa(DumpContext& ctx, uint32_t vaddr)
{
	uint64_t ret = ULLONG_MAX;
	if(ctx.PAE) {
		uint32_t PDPI = 0, PDI = 0, PTI = 0;
		
		PDPI = vaddr >> 30; if(!ctx.TLBpae.sPDPT.sPDPTE[PDPI]) return ret;
		PDI = (vaddr >> 21) & 0x1FF; if(!ctx.TLBpae.sPDPT.sPDPTE[PDPI]->sPDE[PDI]) return ret;
		
		auto Pde = ctx.TLBpae.sPDPT.sPDPTE[PDPI]->sPDE[PDI];
		if(Pde->LargePage) {
			ret = Pde->Pte[0] + (vaddr & 0x1FFFFF);
		} else {
			PTI = (vaddr >> 12) & 0x1FF; if(!(Pde->Pte[PTI] & 1)) return ret;
			ret = PageBase_PAE(Pde->Pte[PTI]) + (vaddr & 0xFFF);
		}
	} else {
	}
	return ret;
}

void DisplayVirtualMemory(DumpContext& ctx, uint32_t va, uint32_t size, int lineLength, int sepSize, bool showChars)
{
	static uint8_t page[4096];
	uint32_t vaUpperbound = va + size;
	char filler[] = "                 "; filler[sepSize * 2 + 1] = 0;
	auto width = cout.width();
	cout << hex << uppercase << setfill('0') ;
	// Check for page bound, round to 4K first
	uint32_t pagebase = VaToPa(ctx, va & (~0xFFF));
	cout << " - PA page base = " << pagebase << endl;
	// Read an entire page, it's not a lot slower but definitely easier
	while(va < vaUpperbound) {
		ReadPhysicalAddress(ctx, pagebase, page);
//		uint32_t readSize = min(vaUpperbound - va, (va & 0xFFF ? va & 0xFFF : 4096));
		int inPageOffset = va % 0x1000;
		if(va % lineLength) { // Align to line bounds
			cout << setw(8) << va - (va % lineLength) << " | ";
			const uint32_t remainderInLine = lineLength - (va % lineLength);
			for(uint32_t ii = 0; ii < va % lineLength; ii += sepSize) cout << filler;
			for(uint32_t ii = 0; ii < remainderInLine; ii++) cout << setw(2) << int(page[inPageOffset + ii]) << ' ';
			if(showChars){
				for(uint32_t ii = 0; ii < va % lineLength; ii += sepSize) cout << ' ';
				for(uint32_t ii = 0; ii < remainderInLine; ii++) {char c = page[inPageOffset + ii]; cout << ((c > 0x19)?c:'.');}
			}
			cout << '\n';
			va += remainderInLine; inPageOffset += remainderInLine;
		}
		while(inPageOffset < 0x1000 && va < vaUpperbound) {
			cout << setw(8) << va << " | ";
			for(int ii = 0; ii < lineLength; ii++) cout << setw(2) << int(page[inPageOffset + ii]) << ' ';
			if(showChars)
				for(int ii = 0; ii < lineLength; ii++) {char c = page[inPageOffset + ii]; cout << ((c > 0x19)?c:'.'); }
			cout << '\n';
			va += lineLength; inPageOffset += lineLength;
		}
	}
	cout << dec << nouppercase << setfill(' ') << setw(width);
}

void DisplayPhysicalMemory(DumpContext& ctx, uint32_t pa, uint32_t size, int lineLength, int sepSize, bool showChars)
{
	static uint8_t page[4096];
	uint32_t paUpperbound = pa + size;
	char filler[10] = "         "; filler[sepSize + 1] = 0;
	auto width = cout.width();
	cout << hex << uppercase << setfill('0') ;
	// Check for page bound, round to 4K first
	uint32_t pagebase = PageBase_PAE(pa & (~0xFFF));
	cout << " - PA page base = " << pagebase << endl;
	// Read an entire page, it's not a lot slower but definitely easier
	while(pa < paUpperbound) {
		ReadPhysicalAddress(ctx, pagebase, page);
//		uint32_t readSize = min(paUpperbound - pa, (pa & 0xFFF ? pa & 0xFFF : 4096));
		int inPageOffset = pa % 0x1000;
		if(pa % lineLength) { // Align to line bounds
			cout << setw(8) << pa - (pa % lineLength) << " | ";
			const uint32_t remainderInLine = lineLength - (pa % lineLength);
			for(uint32_t ii = 0; ii < pa % lineLength; ii += sepSize) cout << filler;
			for(uint32_t ii = 0; ii < remainderInLine; ii++) cout << setw(2) << int(page[inPageOffset + ii]) << ' ';
			if(showChars) {
				for(uint32_t ii = 0; ii < pa % lineLength; ii += sepSize) cout << ' ';
				for(uint32_t ii = 0; ii < remainderInLine; ii++) {char c = page[inPageOffset + ii]; cout << ((c > 0x19)?c:'.');}
			}
			cout << '\n';
			pa += remainderInLine; inPageOffset += remainderInLine;
		}
		while(inPageOffset < 0x1000 && pa < paUpperbound) {
			cout << setw(8) << pa + inPageOffset << " | ";
			for(int ii = 0; ii < lineLength; ii++) cout << setw(2) << int(page[inPageOffset + ii]) << ' ';
			if(showChars)
				for(int ii = 0; ii < lineLength; ii++) {char c = page[inPageOffset + ii]; cout << ((c > 0x19)?c:'.');}
			cout << '\n';
			pa += lineLength; inPageOffset += lineLength;
		}
	}
	cout << dec << nouppercase << setfill(' ') << setw(width);
}

void InteractiveSession(DumpContext& ctx)
{
	string lineBuffer;
	cout << "Ready\n";
	while(true) {
		cout << "CMD> ";
		getline(cin, lineBuffer);
		if(lineBuffer.empty()) continue;
		if(lineBuffer == "Q") break;
		switch(lineBuffer[0]) {
		case 'd': { // Display memory
			stringstream cmdss(lineBuffer);
			string cmd; cmdss >> cmd;
			if(cmd.size() != 2) { cout << "Invalid command\n"; break; }
			int sepSize = 1;
			switch(cmd[1]) {
				case 'b': sepSize = 1; break;
				case 'w': sepSize = 2; break;
				case 'd': sepSize = 4; break;
				case 'q': sepSize = 8; break;
			}
			string addr; cmdss >> addr; if(addr.empty()) { cout << "No address provided\n"; break; }
			uint32_t addri;
			try { addri = stoul(addr, 0, 16); } catch (...) { cout << "Invalid address\n"; break; }
			string qual; cmdss >> qual;
			if(qual.find_first_of('P') != qual.npos)
				DisplayPhysicalMemory(ctx, addri, 256, 16, sepSize, true);
			else
				DisplayVirtualMemory(ctx, addri, 256, 16, sepSize, true);
		}
		default: break;
		}
	}
}

int main()
{
	string path;
	uint32_t u32b, CR3, BitmapBytes;
	uint8_t *Bitmap, IsPae;
	DumpContext ctx;
	
	cout << "Summary dump file: >>> ";
	getline(cin, path);
	
	ifstream& dump = ctx.File;
	dump.open(path, ios::binary | ios::in);
	if(!dump) return cout << "Cannot open file.", 1;
	
	// Verify 32bit and Summary dump
	dump.seekg(0x4); dump.read(TOSTR(&u32b), 4);
	if(u32b != 'PMUD') return cout << "Not 32 bit dump file.\n", 1;
	dump.seekg(0xF88); dump.read(TOSTR(&u32b), 4);
	if(u32b != 2) return cout << "Not summary dump file.\n", 1;
	
	// Read physical memory bitmap
	dump.seekg(0x1010); dump.read(TOSTR(&u32b), 4);
	BitmapBytes = (u32b + 7) / 8;
	Bitmap = new uint8_t[BitmapBytes];
	dump.seekg(0x1020); dump.read((char*)Bitmap, BitmapBytes);
	ctx.PagesBitmap = Bitmap;
	cout << "Bitmap size " << u32b << " Bytes=" << BitmapBytes << '\n';
	
	// Read PAE state
	dump.seekg(0x5C); dump.read(TOSTR(&IsPae), 1);
	cout << "PAE: " << (IsPae ? "ON " : "OFF ");
	ctx.PAE = IsPae;
	
	// Get header size (pages offset)
	dump.seekg(0x100C); dump.read(TOSTR(&ctx.PagesOffset), 4);
	
	// Get CR3
	dump.seekg(0x10); dump.read(TOSTR(&CR3), 4);

	ReadPhysicalAddress(ctx, CR3, u32b);
	cout << hex << "CR3 = 0x" << CR3
		 << dec << ". Importing PTEs..." << endl;

	cout << PopulateTlb(ctx, CR3) << " PTEs imported\n";
	
	InteractiveSession(ctx);
	
	delete[] Bitmap;
	
	return 0;
}

