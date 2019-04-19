/*
 * Copyright 2019 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_BYTECODEGENERATOR
#define SKSL_BYTECODEGENERATOR

#include <stack>
#include <tuple>
#include <unordered_map>

#include "SkSLByteCode.h"
#include "SkSLCodeGenerator.h"
#include "SkSLMemoryLayout.h"
#include "ir/SkSLBinaryExpression.h"
#include "ir/SkSLBoolLiteral.h"
#include "ir/SkSLBlock.h"
#include "ir/SkSLBreakStatement.h"
#include "ir/SkSLConstructor.h"
#include "ir/SkSLContinueStatement.h"
#include "ir/SkSLDoStatement.h"
#include "ir/SkSLExpressionStatement.h"
#include "ir/SkSLFloatLiteral.h"
#include "ir/SkSLIfStatement.h"
#include "ir/SkSLIndexExpression.h"
#include "ir/SkSLInterfaceBlock.h"
#include "ir/SkSLIntLiteral.h"
#include "ir/SkSLFieldAccess.h"
#include "ir/SkSLForStatement.h"
#include "ir/SkSLFunctionCall.h"
#include "ir/SkSLFunctionDeclaration.h"
#include "ir/SkSLFunctionDefinition.h"
#include "ir/SkSLNullLiteral.h"
#include "ir/SkSLPrefixExpression.h"
#include "ir/SkSLPostfixExpression.h"
#include "ir/SkSLProgramElement.h"
#include "ir/SkSLReturnStatement.h"
#include "ir/SkSLStatement.h"
#include "ir/SkSLSwitchStatement.h"
#include "ir/SkSLSwizzle.h"
#include "ir/SkSLTernaryExpression.h"
#include "ir/SkSLVarDeclarations.h"
#include "ir/SkSLVarDeclarationsStatement.h"
#include "ir/SkSLVariableReference.h"
#include "ir/SkSLWhileStatement.h"
#include "spirv.h"

namespace SkSL {

class ByteCodeGenerator : public CodeGenerator {
public:
    class LValue {
    public:
        LValue(ByteCodeGenerator& generator)
            : fGenerator(generator) {}

        virtual ~LValue() {}

        /**
         * Stack before call: ... lvalue
         * Stack after call: ... lvalue load
         */
        virtual void load() = 0;

        /**
         * Stack before call: ... lvalue value
         * Stack after call: ...
         */
        virtual void store() = 0;

    protected:
        ByteCodeGenerator& fGenerator;
    };

    ByteCodeGenerator(const Context* context, const Program* program, ErrorReporter* errors,
                      ByteCode* output)
    : INHERITED(program, errors, nullptr)
    , fContext(*context)
    , fOutput(output) {}

    bool generateCode() override;

    void write8(uint8_t b);

    void write16(uint16_t b);

    void write32(uint32_t b);

    void write(ByteCodeInstruction inst);

    /**
     * Based on 'type', writes the s (signed), u (unsigned), or f (float) instruction.
     */
    void writeTypedInstruction(const Type& type, ByteCodeInstruction s, ByteCodeInstruction u,
                               ByteCodeInstruction f);

    /**
     * Pushes the storage location of an lvalue to the stack.
     */
    void writeTarget(const Expression& expr);

private:
    // reserves 16 bits in the output code, to be filled in later with an address once we determine
    // it
    class DeferredLocation {
    public:
        DeferredLocation(ByteCodeGenerator* generator)
            : fGenerator(*generator)
            , fOffset(generator->fCode->size()) {
            generator->write16(0);
        }

#ifdef SK_DEBUG
        ~DeferredLocation() {
            SkASSERT(fSet);
        }
#endif

        void set() {
            int target = fGenerator.fCode->size();
            SkASSERT(target <= 65535);
            (*fGenerator.fCode)[fOffset] = target >> 8;
            (*fGenerator.fCode)[fOffset + 1] = target;
#ifdef SK_DEBUG
            fSet = true;
#endif
        }

    private:
        ByteCodeGenerator& fGenerator;
        size_t fOffset;
#ifdef SK_DEBUG
        bool fSet = false;
#endif
    };

    /**
     * Returns the local slot into which var should be stored, allocating a new slot if it has not
     * already been assigned one. Compound variables (e.g. vectors) will consume more than one local
     * slot, with the getLocation return value indicating where the first element should be stored.
     */
    int getLocation(const Variable& var);

    std::unique_ptr<ByteCodeFunction> writeFunction(const FunctionDefinition& f);

    void writeVarDeclarations(const VarDeclarations& decl);

    void writeVariableReference(const VariableReference& ref);

    void writeExpression(const Expression& expr);

    /**
     * Pushes whatever values are required by the lvalue onto the stack, and returns an LValue
     * permitting loads and stores to it.
     */
    std::unique_ptr<LValue> getLValue(const Expression& expr);

    void writeFunctionCall(const FunctionCall& c);

    void writeConstructor(const Constructor& c);

    void writeFieldAccess(const FieldAccess& f);

    void writeSwizzle(const Swizzle& swizzle);

    void writeBinaryExpression(const BinaryExpression& b);

    void writeTernaryExpression(const TernaryExpression& t);

    void writeIndexExpression(const IndexExpression& expr);

    void writeLogicalAnd(const BinaryExpression& b);

    void writeLogicalOr(const BinaryExpression& o);

    void writeNullLiteral(const NullLiteral& n);

    void writePrefixExpression(const PrefixExpression& p);

    void writePostfixExpression(const PostfixExpression& p);

    void writeBoolLiteral(const BoolLiteral& b);

    void writeIntLiteral(const IntLiteral& i);

    void writeFloatLiteral(const FloatLiteral& f);

    void writeStatement(const Statement& s);

    void writeBlock(const Block& b);

    void writeBreakStatement(const BreakStatement& b);

    void writeContinueStatement(const ContinueStatement& c);

    void writeIfStatement(const IfStatement& stmt);

    void writeForStatement(const ForStatement& f);

    void writeWhileStatement(const WhileStatement& w);

    void writeDoStatement(const DoStatement& d);

    void writeSwitchStatement(const SwitchStatement& s);

    void writeReturnStatement(const ReturnStatement& r);

    // updates the current set of breaks to branch to the current location
    void setBreakTargets();

    // updates the current set of continues to branch to the current location
    void setContinueTargets();

    const Context& fContext;

    ByteCode* fOutput;

    const FunctionDefinition* fFunction;

    std::vector<uint8_t>* fCode;

    std::vector<const Variable*> fLocals;

    std::stack<std::vector<DeferredLocation>> fContinueTargets;

    std::stack<std::vector<DeferredLocation>> fBreakTargets;

    int fParameterCount;

    friend class DeferredLocation;
    friend class ByteCodeVariableLValue;

    typedef CodeGenerator INHERITED;
};

}

#endif
