#include <string>
#include <cstdio>
#include <cctype>
#include <memory>
#include <utility>
#include <vector>
#include <map>

// 1. LEXER

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
	tok_eof = -1,

	// commands
	tok_def = -2,
	tok_extern = -3,

	// primary
	tok_identifier = -4,
	tok_number = -5,
};

static std::string IdentifierStr; // filled in if tok_identifier
static double NumVal; // Fiiled in if tok_number

// gettok - Return the next token from standard input
static int gettok() {
	static int LastChar = ' ';

	while(isspace(LastChar)) {
		LastChar = std::getchar();
	}

	if(std::isalpha(LastChar)) {
		IdentifierStr = LastChar;
		while (std::isalnum((LastChar = getchar()))) {
			IdentifierStr += LastChar;
		}

		if(IdentifierStr == "def") {
			return tok_def;
		}
		if(IdentifierStr == "extern") {
			return tok_extern;
		}
		return tok_identifier;
	}
	if(std::isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
		std::string NumStr;
		do {
			NumStr += LastChar;
			LastChar = std::getchar();
		} while (std::isdigit(LastChar) || LastChar == '.');

		NumVal = std::stod(NumStr, 0);
		return tok_number;
	}
	if(LastChar == '#') {
		do {
			LastChar = getchar();
		} while(LastChar != EOF && LastChar != '\n' && LastChar != '\r');
		if(LastChar != EOF) {
			return gettok();
		}
	}
	if(LastChar == EOF) {
		return tok_eof;
	}

	int ThisChar = LastChar;
	LastChar = getchar();
	return ThisChar;
}

// 2. AST

// ExprAST - Base class for all expression nodes
class ExprAST {
public:
	virtual ~ExprAST() { }
};

/// NumberExprAST - Expression class for numeric literals
class NumberExprAST: public ExprAST {
	double Val;
public:
	NumberExprAST(double Val): Val(Val) {}
};

/// VariableExprAST - Expression class for referencing a variable
class VariableExprAST: public ExprAST {
	std::string Name;
public:
	VariableExprAST(const std::string& Name): Name(Name) { }
};

// BinaryExprAST - Expression class for a binary operator
class BinaryExprAST: public ExprAST {
	char Op;
	std::unique_ptr<ExprAST> LHS, RHS;
public:
	BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS,
				  std::unique_ptr<ExprAST> RHS)
	: Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) { }
};

/// CallExprAST - Expression class for function calls.
class CallExprAST: public ExprAST {
	std::string Callee;
	std::vector<std::unique_ptr<ExprAST>> Args;
public:
	CallExprAST(const std::string& Callee,
				std::vector<std::unique_ptr<ExprAST>> Args)
	: Callee(Callee), Args(std::move(Args)) { }
};

/// PrototypeAST - This class represents the "prototype"
/// which captures its name, and its argument names (thus, number
// of arguments the function takes).
class PrototypeAST {
	std::string Name;
	std::vector<std::string> Args;
public:
	PrototypeAST(const std::string& name, std::vector<std::string> Args)
	: Name(name), Args(std::move(Args)) { }
	const std::string& getName() const {
		return Name;
	}
};

/// FunctionAST - This class represents a function definition
class FunctionAST {
	std::unique_ptr<PrototypeAST> Proto;
	std::unique_ptr<ExprAST> Body;
public:
	FunctionAST(std::unique_ptr<PrototypeAST> Proto,
				std::unique_ptr<ExprAST> Body)
	: Proto(std::move(Proto)), Body(std::move(Body)) { }
};

// 3. PARSER

/// CurTok/getNextToken - Provide a simple token buffer.
/// token the parser is looking at. getNextToken reads another token from the
// lexer and updates CurTok with its results.

static std::unique_ptr<ExprAST> ParseExpression();
static std::unique_ptr<ExprAST> ParseBinOpRHS(int, std::unique_ptr<ExprAST>);

static int CurTok;
static int getNextToken() {
	return CurTok = gettok();
}

std::unique_ptr<ExprAST> LogError(const char* Str) {
	std::fprintf(stderr, "LogError: %s\n", Str);
	return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char* Str) {
	LogError(Str);
	return nullptr;
}

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
	auto Result = std::make_unique<NumberExprAST>(NumVal);
	getNextToken(); // consume the number
	return std::move(Result);
}

/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
	getNextToken(); // eat (
	auto V = ParseExpression();
	if(!V) {
		return nullptr;
	}
	if(CurTok != ')') {
		return LogError("expected ')'");
	}
	getNextToken(); // eat )
	return V;
}

/// identifierxpr
///		::= identifier
//		::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
	std::string IdName = IdentifierStr;

	getNextToken(); // eat identifier

	if (CurTok != '(') {
		return std::make_unique<VariableExprAST>(IdName);
	}

	// Call
	getNextToken(); // eat (
	std::vector<std::unique_ptr<ExprAST>> Args;
	if(CurTok != ')') {
		while(1) {
			if(auto Arg = ParseExpression()) {
				Args.push_back(std::move(Arg));
			} else {
				return nullptr;
			}

			if(CurTok == ')') {
				break;
			}


			if(CurTok != ',') {
				return LogError("Expected ')' or ',' in argument list");
			}
			getNextToken();
		}
	}

	// Eat the ')'
	getNextToken();

	return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// primary
///		::= identifierexpr
///		::= numberexpr
///		::= parenexpr

static std::unique_ptr<ExprAST> ParsePrimary() {
	switch(CurTok) {
	default:
		return LogError("unknown token when expecting an expression");
	case tok_identifier:
		return ParseIdentifierExpr();
	case tok_number:
		return ParseNumberExpr();
	case '(':
		return ParseParenExpr();
	}
}

/// BinopPrecedence - This holds the precedence for each binary operation that's
/// defined.
static std::map<char, int> BinopPrecedence{
	{'<', 10}, {'+', 20}, {'-', 20}, {'*', 40}, {'/', 40}
};

/// GetTokPrecedence - Get the precedence of the pending token
static int GetTokPrecedence() {
	if(!isascii(CurTok)) {
		return -1;
	}

	// Make sure it's a declared binop
	int TokPrec = BinopPrecedence[CurTok];
	if (TokPrec <= 0) return -1;
	return TokPrec;
}

/// expression
///		::= primary binoprhs
///
static std::unique_ptr<ExprAST> ParseExpression() {
	auto LHS = ParsePrimary();
	if(!LHS) {
		return nullptr;
	}
	return ParseBinOpRHS(0, std::move(LHS));
}

/// binoprhs
///		::= ('+' primary)*
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
											  std::unique_ptr<ExprAST> LHS) {
	// If this is a binop, find its precedence
	while (1) {
		int TokPrec = GetTokPrecedence();

		// If this is a binop that binds at least as tightly as the current binop,
		// consume it, otherwise we are done

		if (TokPrec < ExprPrec) {
			return LHS;
		}

		// Okay, we know this is a binop
		int BinOp = CurTok;
		getNextToken(); // eat binop

		// Parse the primary expression after the binary operator
		auto RHS = ParsePrimary();
		if (!RHS) {
			return nullptr;
		}

		// If BinOp binds less tightly with RHS than the operator after RHS, let
		// the pending operator take RHS as its LHS.
		int NextPrec = GetTokPrecedence();
		if(TokPrec < NextPrec) {
			RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
			if (!RHS) {
				return nullptr;
			}
		}

		// Merge LHS/RHS.
		LHS = std::make_unique<BinaryExprAST>(
			BinOp, std::move(LHS), std::move(RHS)
		);
	}
}

/// prototype
///		::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
	if(CurTok != tok_identifier) {
		return LogErrorP("Expected function name in prototype");
	}

	std::string FnName = IdentifierStr;
	getNextToken();

	if(CurTok != '(') {
		return LogErrorP("Expected '(' in prototype");
	}

	// Read the list of argument names
	std::vector<std::string> ArgNames;
	while (getNextToken() == tok_identifier) {
		ArgNames.push_back(IdentifierStr);
	}
	if(CurTok != ')') {
		return LogErrorP("Expected ')' in prototype");
	}

	// success
	getNextToken(); // eat ')'

	return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
	getNextToken(); // eat def
	auto Proto = ParsePrototype();
	if(!Proto) return nullptr;

	if(auto E = ParseExpression()) {
		return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
	}
	return nullptr;
}

/// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
	getNextToken(); // eat extern
	return ParsePrototype();
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
	if(auto E = ParseExpression()) {
		// Make an anonymous proto
		auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
		return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
	}
	return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing
//===----------------------------------------------------------------------===//

static void HandleDefinition() {
  if (ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition.\n");
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (ParseExtern()) {
    fprintf(stderr, "Parsed an extern\n");
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (ParseTopLevelExpr()) {
    fprintf(stderr, "Parsed a top-level expr\n");
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
	while(1) {
		printf("ready> ");
		switch(CurTok) {
		case tok_eof:
			return;
		case ';':
			getNextToken();
			break;
		case tok_def:
			HandleDefinition();
			break;
		case tok_extern:
			HandleExtern();
			break;
		default:
			HandleTopLevelExpression();
			break;
		}
	}
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
	printf("ready> ");

	getNextToken();
	MainLoop();
}