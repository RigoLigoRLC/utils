
// neslof - nested loop formatting

#include <iostream>
#include <vector>
#include <string>
#include <cstring>

using std::vector;
using std::cout;
using std::cerr;
using std::endl;
using std::string;

struct LoopLayer {
	vector<string> formattedComponents;
	size_t index;
};

struct FormatSegment {
	string formatStringPart;
	size_t compIndexFollowing;
	bool isStaticCompspec;
};

inline bool NumericAscii(char c) { return (c >= '0' && c <= '9'); }

void ShowHelp() {
	cout << "Args: format-string compspec1 compspec2 ...\n\n"
			"format-string:\n"
			"  %1 %2 ... %n: Insert component spec here\n"
			"  %S: static component, increment once per line, expanded to last\n"
			"      compspec in the list, can exist only once in format string\n"
			"  %%: escape %\n\n"
			"compspec:\n"
			"  Numeric: N/a From integer 0 to a, [0, a]"
			" 			N/a,b From integer a to b, [a, b]\n"
			"           N/a,b,s From integer a to b step s\n"
			"  Float: F/a,b From a to b increment 1.0\n"
			"   	  F/a,b,c From a to b increment c\n"
			"  Text list: T/foo,bar\n"
			"    \\, - escape ,\n"
			"    \\\\ - escape \\\n"
		<< endl;
	;
}

bool parseFormatString(const char* str, vector<FormatSegment>& out) {
	size_t length = strlen(str);
	const char* begin = str;
	const char* s = str;
	bool hasStaticCompspec = false, isStaticCompspec = false;
	size_t parsedIndex = 0, parsedIndexLength = 0;
	string buffer;
	
	enum { Normal, MetPercent, ParseIndex } state = Normal;
	while (*s != '\0') {
		char c = *s;
		switch (state) {
		case Normal:
			if (c == '%') {
				state = MetPercent;
			}
			break;
			
		case MetPercent:
			buffer.append(begin, s); // Needed in all cases
			begin = s;
			if (c == '%') {
				// Escaped %
				begin++;
				state = Normal;
			} else {
				state = ParseIndex;
				buffer.pop_back(); // Remove the % 
				// Go back one character so this one is parsed again in normal mode
				s--;
			}
			break;
			
		case ParseIndex:
			if (!NumericAscii(c)) {
				if (parsedIndexLength == 0) {
					if (c == 'S') {
						isStaticCompspec = true;
						begin = s + 1;
					} else {
						cerr << "Invalid format index at offset " << (s - begin) << ": \"" << c << "\"!\n";
						return false;
					}
				}
				if (!isStaticCompspec && parsedIndex == 0) {
					cerr << "Invalid format index: %0 is not allowed\n";
					return false;
				}
				if (hasStaticCompspec && isStaticCompspec) {
					cerr << "Multiple static compspecs\n";
					return false;
				}
				out.emplace_back(FormatSegment { buffer, parsedIndex, isStaticCompspec });
				buffer.clear();
				if (!isStaticCompspec) {
					begin = s; // Don't worry about bounds, for loop will guard this
					s--;
				}
				state = Normal;
				isStaticCompspec = false;
				parsedIndexLength = 0;
				parsedIndex = 0;
			} else {
				parsedIndexLength++;
				parsedIndex *= 10;
				parsedIndex += (c - '0');
			}
			break;
		}
		s++;
	}
	
	if (state == ParseIndex) {
		out.emplace_back(FormatSegment { buffer, parsedIndex, isStaticCompspec });
	} else if (begin < str + length) {
		buffer.append(begin, str + length);
		out.emplace_back(FormatSegment { buffer, 0, isStaticCompspec });
	}
	
	return true;
}

bool parseCompspecNumeric(const char* str, LoopLayer& out, size_t count = 0) {
	vector<size_t> parsedNumbers;
	size_t parsedNumber = 0;
	
	auto s = str;
	while (true) {
		char c = *s;
		if (c == ',' || c == '\0') {
			parsedNumbers.emplace_back(parsedNumber);
			parsedNumber = 0;
			if (c == '\0') break;
		} else if (NumericAscii(c)) {
			parsedNumber *= 10;
			parsedNumber += (c - '0');
		} else {
			cerr << "Invalid numeric compspec: invalid character \"" << c << "\"!\n";
			return false;
		}
		s++;
	}
	
	switch(parsedNumbers.size()) {
	case 1:
		if (count) parsedNumbers[0] = count - 1;
		for (size_t i = 0; i <= parsedNumbers[0]; i++) {
			out.formattedComponents.emplace_back(std::to_string(i));
		}
		break;

	case 2:
		if (parsedNumbers[0] > parsedNumbers[1]) {
			cerr << "Invalid numeric compspec \"" << str << "\": begin > end\n";
			return false;
		}
		if (count) parsedNumbers[1] = parsedNumbers[0] + count - 1;
		for (size_t i = parsedNumbers[0]; i <= parsedNumbers[1]; i++) {
			out.formattedComponents.emplace_back(std::to_string(i));
		}
		break;
		
	case 3:
		if (parsedNumbers[0] > parsedNumbers[1]) {
			cerr << "Invalid numeric compspec \"" << str << "\": begin > end\n";
			return false;
		}
		if (count) parsedNumbers[1] = parsedNumbers[0] + (count - 1) * parsedNumbers[2];
		for (size_t i = parsedNumbers[0]; i <= parsedNumbers[1]; i += parsedNumbers[2]) {
			out.formattedComponents.emplace_back(std::to_string(i));
		}
		break;
		
	default:
		cerr << "Invalid numeric compspec \"" << str << "\": Invalid amount of numbers given\n";
		return false;
	}
	
	return true;
}

bool parseCompspecText(const char *str, LoopLayer &out, size_t count = 0) {
	enum { Normal, MetBackslash } state = Normal;
	const char* begin = str;
	const char* s = str;
	string buffer;
	
	while (*s != '\0') {
		char c = *s;
		switch(state) {
		case Normal:
			if (c == ',') {
				buffer.append(begin, s);
				out.formattedComponents.emplace_back(buffer);
				buffer.clear();
				begin = s + 1;
			} else if (c == '\\') {
				state = MetBackslash;
			}
			break;
		case MetBackslash:
			switch(c) {
			case '\\':
				buffer.append(begin, s);
				begin = s + 1;
				break;
			case ',':
				buffer.append(begin, s - 1);
				buffer.append(",");
				begin = s + 1;
				break;
			default:
				cerr << "Invalid text compspec \"" << str << "\": Invalid escape sequence \\" << c << "!\n";
				return false;
			}
			break;
		}
		s++;
	}
	
	if (s > begin) {
		out.formattedComponents.emplace_back(string(begin, s));
	}
	
	const auto stringCount = out.formattedComponents.size();
	for (size_t i = stringCount; i < count; i++) {
		// Infinitely fill with same pattern
		out.formattedComponents.emplace_back(out.formattedComponents[i % stringCount]);
	}
	
	return true;
}

bool parseCompspecFloat(const char *str, LoopLayer &out, size_t count = 0) {
	enum { Integer, Decimal } state = Integer;
	const char* s = str;
	double parsedFloat = 0.0, coeff = 1;
	size_t parsedLength = 0;
	vector<double> parsedFloats;
	string buffer;
	
	while (*s != '\0') {
		char c = *s;
		switch (state) {
		case Integer:
			if (NumericAscii(c)) {
				parsedLength++;
				parsedFloat *= 10;
				parsedFloat += (c - '0');
			} else if (c == '.') {
				parsedLength++;
				state = Decimal;
			} else if (c == ',') {
				parsedFloats.emplace_back(parsedFloat);
				parsedLength = 0;
				parsedFloat = 0.0;
			} else {
				cerr << "Invalid float compspec \"" << str << "\": Invalid character " << c << "!\n";
				return false;
			}
			break;
		
		case Decimal:
			coeff *= 0.1;
			if (NumericAscii(c)) {
				parsedLength++;
				parsedFloat += coeff * (c - '0');
			} else if (c == ',') {
				parsedFloats.emplace_back(parsedFloat);
				parsedLength = 0;
				parsedFloat = 0.0;
				coeff = 1.0;
				state = Integer;
			} else {
				cerr << "Invalid float compspec \"" << str << "\": Invalid character " << c << "!\n";
				return false;
			}
			break;
		}
		
		s++;
	}
	
	if (parsedLength != 0) {
		parsedFloats.emplace_back(parsedFloat);
	}
	
	switch(parsedFloats.size()) {		
	case 2:
		if (parsedFloats[0] > parsedFloats[1]) {
			cerr << "Invalid float compspec \"" << str << "\": begin > end\n";
			return false;
		}
		if (count) parsedFloats[1] = parsedFloats[0] + count - 1;
		for (double i = parsedFloats[0]; i <= parsedFloats[1]; i += 1) {
			out.formattedComponents.emplace_back(std::to_string(i));
		}
		break;
		
	case 3:
		if (parsedFloats[0] > parsedFloats[1]) {
			cerr << "Invalid float compspec \"" << str << "\": begin > end\n";
			return false;
		}
		if (count) parsedFloats[1] = parsedFloats[0] + (count - 1) * parsedFloats[2];
		for (double i = parsedFloats[0]; i <= parsedFloats[1]; i += parsedFloats[2]) {
			out.formattedComponents.emplace_back(std::to_string(i));
		}
		break;
		
	default:
		cerr << "Invalid float compspec \"" << str << "\": Invalid amount of numbers given\n";
		return false;
	}
	
	return true;
}

bool parseCompspecs(size_t count, const char** strs, vector<LoopLayer> &out, bool hasStaticComp) {
	for (size_t i = 0; i < count; i++) {
		const char* str = strs[i];
		char type = *str;
		if (strlen(str) <= 2) {
			cerr << "Compspec \"" << str << "\" too short!\n";
			return false;
		}
		str += 2;
		LoopLayer layer;
		size_t staticCount = 0;
		
		if (hasStaticComp) {
			// Calculate static component count
			for(auto &i : out) {
				staticCount *= i.formattedComponents.size();
			}
		}
		
		switch(type) {
		case 'N':
			if (!parseCompspecNumeric(str, layer, staticCount)) return false;
			out.emplace_back(layer);
			break;
		case 'T':
			if (!parseCompspecText(str, layer, staticCount)) return false;
			out.emplace_back(layer);
			break;
		case 'F':
			if (!parseCompspecFloat(str, layer, staticCount)) return false;
			out.emplace_back(layer);
			break;
		default:
			cerr << "Unsupported compspec type " << type << "!\n";
			return false;
		}
		
	}
	
	return true;
}

int main(int argc, char** argv) {
	if (argc <= 1) {
		ShowHelp();
		return 0;
	}
	
	vector<FormatSegment> formatStringParts;
	vector<LoopLayer> componentLists;
	LoopLayer staticLayer;
	size_t staticCompIndex = SIZE_MAX, staticLoopVar = 0;
	
	if(!parseFormatString(argv[1], formatStringParts)) return 1;
	// Find out whether we have static component
	for(size_t i = 0; i < formatStringParts.size(); i++)
		if (formatStringParts[i].isStaticCompspec) {
			staticCompIndex = i;
			break;
		}
	
	if(!parseCompspecs(argc - 2,
					   const_cast<const char**>(argv + 2),
					   componentLists,
					   staticCompIndex != SIZE_MAX)
		) return 1;
	// Take out last static component if we had one
	if (staticCompIndex != SIZE_MAX) {
		staticLayer = componentLists.back();
		componentLists.pop_back();
	}
	
	size_t maxIndex = 0;
	for(auto &i : formatStringParts) {
		if (i.compIndexFollowing > maxIndex) maxIndex = i.compIndexFollowing;
	}
	if (maxIndex > componentLists.size()) {
		cerr << "Format index too large: %" << maxIndex << endl;
		return 1;
	}
	
	vector<size_t> loopVar, limits;
	for (auto &i : formatStringParts) {
		if (i.compIndexFollowing != 0) {
			limits.emplace_back(componentLists[i.compIndexFollowing - 1].formattedComponents.size());
			loopVar.emplace_back(0);
		}
	}
	size_t stackPointer = loopVar.size() - 1;
	
	while (1) {
		// Check if we have just jumped back on the stack
		if (stackPointer < loopVar.size() - 1) {
			// Check if current loop layer is already done
			if (loopVar[stackPointer] == limits[stackPointer]) {
				// All done
				if (stackPointer == 0)
					break;
				// Jump back to last layer of stack
				stackPointer--;
				// That one should increment now
				loopVar[stackPointer]++;
			} else {
				// Goto next layer
				stackPointer++;
				// Initialize next layer
				loopVar[stackPointer] = 0;
			}
		}
		
		string out;
		for (size_t i = 0; i < formatStringParts.size(); i++) {
			auto& o = formatStringParts[i];
			out += o.formatStringPart;
			if (o.compIndexFollowing != 0) {
				out += componentLists[o.compIndexFollowing - 1].formattedComponents[loopVar[i]];
			} else if (o.isStaticCompspec) {
				out += staticLayer.formattedComponents[staticLoopVar++];
			}
		}
		cout << out << '\n';
		
//		cout << "Stack: ";
//		for(auto i : loopVar) {
//			cout << i << ' ';
//		}
//		cout << "SP = " << stackPointer << endl;
		
		if (++loopVar[stackPointer] == limits[stackPointer]) {
			// Jump back to last layer of stack
			stackPointer--;
			// That one should increment now
			loopVar[stackPointer]++;
		}
	}
	
	return 0;
}

//(label "B34_L24N" (at 514.35 45.72 0) (fields_autoplaced) (effects (font (size 1.27 1.27)) (justify left bottom)))

