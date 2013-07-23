/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef ART_COMPILER_SEA_IR_SEA_H_
#define ART_COMPILER_SEA_IR_SEA_H_

#include <set>
#include <map>

#include "dex_file.h"
#include "dex_instruction.h"
#include "instruction_tools.h"
#include "instruction_nodes.h"
#include "utils/scoped_hashtable.h"

namespace sea_ir {

// Reverse post-order numbering constants
enum RegionNumbering {
  NOT_VISITED = -1,
  VISITING = -2
};

class Region;

class InstructionNode;
class PhiInstructionNode;
class SignatureNode;

// A SignatureNode is a declaration of one parameter in the function signature.
// This class is used to provide place-holder definitions to which instructions
// can return from the GetSSAUses() calls, instead of having missing SSA edges.
class SignatureNode: public InstructionNode {
 public:
  explicit SignatureNode(unsigned int parameter_register):
      InstructionNode(NULL), defined_regs_() {
    defined_regs_.push_back(parameter_register);
  }

  void ToDot(std::string& result) const {
    result += StringId() +" [label=\"signature:";
    std::stringstream vector_printer;
    if (!defined_regs_.empty()) {
      for (unsigned int crt_el = 0; crt_el < defined_regs_.size()-1; crt_el++) {
        vector_printer << defined_regs_[crt_el] <<",";
      }
      vector_printer << defined_regs_[defined_regs_.size()-1] <<";";
    }
    result += "\"] // signature node\n";
  }

  std::vector<int> GetDefinitions() const {
    return defined_regs_;
  }

  int GetResultRegister() const {
    return NO_REGISTER;
  }

  std::vector<int> GetUses() {
    return std::vector<int>();
  }

  void Accept(IRVisitor* v) {
    v->Visit(this);
    v->Traverse(this);
  }

 private:
  std::vector<int> defined_regs_;
};

class PhiInstructionNode: public InstructionNode {
 public:
  explicit PhiInstructionNode(int register_no):
    InstructionNode(NULL), register_no_(register_no), definition_edges_() {}
  // Appends to @result the .dot string representation of the instruction.
  void ToDot(std::string& result) const;
  // Returns the register on which this phi-function is used.
  int GetRegisterNumber() const {
    return register_no_;
  }

  // Renames the use of @reg_no to refer to the instruction @definition.
  // Phi-functions are different than normal instructions in that they
  // have multiple predecessor regions; this is why RenameToSSA has
  // the additional parameter specifying that @parameter_id is the incoming
  // edge for @definition, essentially creating SSA form.
  void RenameToSSA(int reg_no, InstructionNode* definition, unsigned int predecessor_id) {
    DCHECK(NULL != definition) << "Tried to rename to SSA using a NULL definition for "
        << StringId() << " register " << reg_no;
    if (definition_edges_.size() < predecessor_id+1) {
      definition_edges_.resize(predecessor_id+1, NULL);
    }
    if (NULL == definition_edges_.at(predecessor_id)) {
      definition_edges_[predecessor_id] = new std::vector<InstructionNode*>();
    }
    definition_edges_[predecessor_id]->push_back(definition);
  }

  // Returns the instruction that defines the phi register from predecessor
  // on position @predecessor_pos. Note that the return value is vector<> just
  // for consistency with the return value of GetSSAUses() on regular instructions,
  // The returned vector should always have a single element because the IR is SSA.
  std::vector<InstructionNode*>* GetSSAUses(int predecessor_pos) {
    return definition_edges_.at(predecessor_pos);
  }

  void Accept(IRVisitor* v) {
    v->Visit(this);
    v->Traverse(this);
  }

 private:
  int register_no_;
  std::vector<std::vector<InstructionNode*>*> definition_edges_;
};

// This class corresponds to a basic block in traditional compiler IRs.
// The dataflow analysis relies on this class both during execution and
// for storing its results.
class Region : public SeaNode {
 public:
  explicit Region():
    SeaNode(), successors_(), predecessors_(), reaching_defs_size_(0),
    rpo_(NOT_VISITED), idom_(NULL), idominated_set_(), df_(), phi_set_() {}
  // Adds @instruction as an instruction node child in the current region.
  void AddChild(sea_ir::InstructionNode* instruction);
  // Returns the last instruction node child of the current region.
  // This child has the CFG successors pointing to the new regions.
  SeaNode* GetLastChild() const;
  // Returns all the child instructions of this region, in program order.
  std::vector<InstructionNode*>* GetInstructions() {
    return &instructions_;
  }
  // Appends to @result a dot language formatted string representing the node and
  //    (by convention) outgoing edges, so that the composition of theToDot() of all nodes
  //    builds a complete dot graph (without prolog and epilog though).
  virtual void ToDot(std::string& result) const;
  // Computes Downward Exposed Definitions for the current node.
  void ComputeDownExposedDefs();
  const std::map<int, sea_ir::InstructionNode*>* GetDownExposedDefs() const;
  // Performs one iteration of the reaching definitions algorithm
  // and returns true if the reaching definitions set changed.
  bool UpdateReachingDefs();
  // Returns the set of reaching definitions for the current region.
  std::map<int, std::set<sea_ir::InstructionNode*>* >* GetReachingDefs();

  void SetRPO(int rpo) {
    rpo_ = rpo;
  }

  int GetRPO() {
    return rpo_;
  }

  void SetIDominator(Region* dom) {
    idom_ = dom;
  }

  Region* GetIDominator() const {
    return idom_;
  }

  void AddToIDominatedSet(Region* dominated) {
    idominated_set_.insert(dominated);
  }

  const std::set<Region*>* GetIDominatedSet() {
    return &idominated_set_;
  }
  // Adds @df_reg to the dominance frontier of the current region.
  void AddToDominanceFrontier(Region* df_reg) {
    df_.insert(df_reg);
  }
  // Returns the dominance frontier of the current region.
  // Preconditions: SeaGraph.ComputeDominanceFrontier()
  std::set<Region*>* GetDominanceFrontier() {
    return &df_;
  }
  // Returns true if the region contains a phi function for @reg_no.
  bool ContainsPhiFor(int reg_no) {
    return (phi_set_.end() != phi_set_.find(reg_no));
  }
  // Returns the phi-functions from the region.
  std::vector<PhiInstructionNode*>* GetPhiNodes() {
    return &phi_instructions_;
  }
  // Adds a phi-function for @reg_no to this region.
  // Note: The insertion order does not matter, as phi-functions
  //       are conceptually executed at the same time.
  bool InsertPhiFor(int reg_no);
  // Sets the phi-function uses to be as defined in @scoped_table for predecessor @@predecessor.
  void SetPhiDefinitionsForUses(const utils::ScopedHashtable<int, InstructionNode*>* scoped_table,
      Region* predecessor);

  void Accept(IRVisitor* v) {
    v->Visit(this);
    v->Traverse(this);
  }

  void AddSuccessor(Region* successor) {
    DCHECK(successor) << "Tried to add NULL successor to SEA node.";
    successors_.push_back(successor);
    return;
  }
  void AddPredecessor(Region* predecessor) {
    DCHECK(predecessor) << "Tried to add NULL predecessor to SEA node.";
    predecessors_.push_back(predecessor);
  }

  std::vector<sea_ir::Region*>* GetSuccessors() {
    return &successors_;
  }
  std::vector<sea_ir::Region*>* GetPredecessors() {
    return &predecessors_;
  }

 private:
  std::vector<sea_ir::Region*> successors_;    // CFG successor nodes (regions)
  std::vector<sea_ir::Region*> predecessors_;  // CFG predecessor nodes (instructions/regions)
  std::vector<sea_ir::InstructionNode*> instructions_;
  std::map<int, sea_ir::InstructionNode*> de_defs_;
  std::map<int, std::set<sea_ir::InstructionNode*>* > reaching_defs_;
  int reaching_defs_size_;
  int rpo_;
  // Immediate dominator node.
  Region* idom_;
  // The set of nodes immediately dominated by the region.
  std::set<Region*> idominated_set_;
  // Records the dominance frontier.
  std::set<Region*> df_;
  // Records the set of register numbers that have phi nodes in this region.
  std::set<int> phi_set_;
  std::vector<PhiInstructionNode*> phi_instructions_;
};

// A SeaGraph instance corresponds to a source code function.
// Its main point is to encapsulate the SEA IR representation of it
// and acts as starting point for visitors (ex: during code generation).
class SeaGraph: IVisitable {
 public:
  static SeaGraph* GetCurrentGraph();

  void CompileMethod(const art::DexFile::CodeItem* code_item,
      uint32_t class_def_idx, uint32_t method_idx, const art::DexFile& dex_file);
  // Returns all regions corresponding to this SeaGraph.
  std::vector<Region*>* GetRegions() {
    return &regions_;
  }
  // Returns a string representation of the region and its Instruction children.
  void DumpSea(std::string filename) const;
  // Recursively computes the reverse postorder value for @crt_bb and successors.
  static void ComputeRPO(Region* crt_bb, int& crt_rpo);
  // Returns the "lowest common ancestor" of @i and @j in the dominator tree.
  static Region* Intersect(Region* i, Region* j);
  //Returns the vector of parameters of the function.
  std::vector<SignatureNode*>* GetParameterNodes() {
    return &parameters_;
  }
  uint32_t class_def_idx_;
  uint32_t method_idx_;

 private:
  SeaGraph(): class_def_idx_(0), method_idx_(0), regions_(), parameters_() {
  }
  // Registers @childReg as a region belonging to the SeaGraph instance.
  void AddRegion(Region* childReg);
  // Returns new region and registers it with the  SeaGraph instance.
  Region* GetNewRegion();
  // Adds a (formal) parameter node to the vector of parameters of the function.
  void AddParameterNode(SignatureNode* parameterNode) {
    parameters_.push_back(parameterNode);
  }
  // Adds a CFG edge from @src node to @dst node.
  void AddEdge(Region* src, Region* dst) const;
  // Builds the non-SSA sea-ir representation of the function @code_item from @dex_file
  // with class id @class_def_idx and method id @method_idx.
  void BuildMethodSeaGraph(const art::DexFile::CodeItem* code_item,
      const art::DexFile& dex_file, uint32_t class_def_idx, uint32_t method_idx);
  // Computes immediate dominators for each region.
  // Precondition: ComputeMethodSeaGraph()
  void ComputeIDominators();
  // Computes Downward Exposed Definitions for all regions in the graph.
  void ComputeDownExposedDefs();
  // Computes the reaching definitions set following the equations from
  // Cooper & Torczon, "Engineering a Compiler", second edition, page 491.
  // Precondition: ComputeDEDefs()
  void ComputeReachingDefs();
  // Computes the reverse-postorder numbering for the region nodes.
  // Precondition: ComputeDEDefs()
  void ComputeRPO();
  // Computes the dominance frontier for all regions in the graph,
  // following the algorithm from
  // Cooper & Torczon, "Engineering a Compiler", second edition, page 499.
  // Precondition: ComputeIDominators()
  void ComputeDominanceFrontier();
  // Converts the IR to semi-pruned SSA form.
  void ConvertToSSA();
  // Performs the renaming phase of the SSA transformation during ConvertToSSA() execution.
  void RenameAsSSA();
  // Identifies the definitions corresponding to uses for region @node
  // by using the scoped hashtable of names @ scoped_table.
  void RenameAsSSA(Region* node, utils::ScopedHashtable<int, InstructionNode*>* scoped_table);

  virtual void Accept(IRVisitor* visitor) {
    visitor->Initialize(this);
    visitor->Visit(this);
    visitor->Traverse(this);
  }

  virtual ~SeaGraph() {}
  // Generate LLVM IR for the method.
  // Precondition: ConvertToSSA().
  void GenerateLLVM();

  static SeaGraph graph_;
  std::vector<Region*> regions_;
  std::vector<SignatureNode*> parameters_;
};
} // end namespace sea_ir
#endif  // ART_COMPILER_SEA_IR_SEA_H_
