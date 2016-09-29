/*
 * =====================================================================================
 *
 *       Filename:  obj-optim.cpp
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  09/19/2016 03:01:11 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  xenia-cjen (xc), jonah0604@gmail.com
 *        Company:  LaDS(I), GIEE, NTU
 *
 * =====================================================================================
 */

#include <cassert>
#include <climits>
#include <algorithm>

#include "atpg.h" 

using namespace std; 

using namespace CoreNs; 

bool Atpg::insertObj(const Objective& obj, ObjList& objs) { 
    // TODO: forward should not skip? 
    if (is_fault_reach_[obj.first]) return true; 

    Value v = impl_->GetVal(obj.first); 
    assert(v==X); 

    pair<ObjListIter, bool> ret = objs.insert(obj); 

    if (!ret.second && ret.first->second!=obj.second)  
        return false; 

    return true; 
} 

bool Atpg::AddUniquePathObj(Gate *gtoprop, queue<Objective>& events) { 
    Gate *gnext = gtoprop; 
    while (gnext->nfo_==1) { 
        for (int i=0; i<gnext->nfi_; i++) { 
            if (impl_->GetVal(gnext->fis_[i])!=X) continue;  
            //   || is_fault_reach_[gnext->fis_[i]]) continue; 
            Objective obj; 
            obj.first = gnext->fis_[i]; 
            obj.second = gnext->getInputNonCtrlValue(); 
            events.push(obj); 
        }
        gnext = &cir_->gates_[gnext->fos_[0]]; 
    }

    return true; 
} 

void Atpg::PushObjEvents(Gate *prev, 
                           const Objective& obj, 
                           queue<Objective>& events, 
                           queue<Objective>& events_forward) { 

    events.push(obj); 

    Gate *g = &cir_->gates_[obj.first]; 
    // TODO: clean foward event list and return 
    // if (g->type_==Gate::PI || g->type_==Gate::PPI) return; 
    if (g->nfo_>1) { 
        for (int i=0; i<g->nfo_; i++) {
            Gate *fo = &cir_->gates_[g->fos_[i]]; 
            // TODO: skip those in objs 
            if (fo==prev) continue; 
            
            Objective obj_forward; 
            if (fo->type_==Gate::BUF || fo->type_==Gate::INV) { 
                obj_forward.first = fo->id_; 
                obj_forward.second = 
                  (fo->isInverse())?EvalNot(obj.second):obj.second;
                events_forward.push(obj_forward); 
            }
            else { 
                if (obj.second==fo->getInputCtrlValue()) { 
                    obj_forward.first = fo->id_; 
                    obj_forward.second = EvalNot(fo->getOutputCtrlValue()); 
                    events_forward.push(obj_forward); 
                }
            }
        }
    }
}

bool Atpg::AddGateToProp(Gate *gtoprop) { 
    Value v = impl_->GetVal(gtoprop->id_); 

    if (v==D || v==B) // D-frontier pushed forward 
        return true; 
    else if (v!=X) // D-frontier compromised 
        return false;  
    else  { 
        if (!CheckXPath(gtoprop)) return false; 

        ObjList objs = objs_; // create a temp. copy 

        Objective obj; 
        obj.first = gtoprop->id_; 
        obj.second = gtoprop->getOutputCtrlValue(); 
    
        assert(!gtoprop->isUnary()); 

        stack<Objective> event_list; 
        queue<Objective> event_list_forward; 
        // if (!AddUniquePathObj(gtoprop, event_list)) return false; 

        event_list.push(obj); 
        while (!event_list.empty()) { 
            obj = event_list.top(); 
            event_list.pop(); 

            SetObjRet ret = SetObj(obj, objs); 
            if (ret==FAIL) return false; 
            else if (ret==NOCHANGE) continue; 
           
            Gate *g = &cir_->gates_[obj.first]; 
            if (g->type_==Gate::BUF || g->type_==Gate::INV) { 
                obj.first = g->fis_[0]; 
                obj.second = (g->isInverse())?EvalNot(obj.second):obj.second;
                event_list.push(obj); 
            } 
            else { 
                if (g->getOutputCtrlValue()==obj.second) { 
                    PushFaninObjEvent(g, event_list); 
                }
                else { 
                    bool success = true; 
                    int x_count = 0; 
                    int gnext = -1; 
                    for (int i=0; i<g->nfi_; i++) { 
                        int fi = g->fis_[i]; 
                        ObjListIter it = objs.find(fi); 
                        if (impl_->GetVal(fi)==X && it==objs.end()) { 
                            x_count++;  
                            gnext = i; 
                        }
                        else if (it!=objs.end() 
                          && it->second==g->getInputCtrlValue()) { 
                            success = false; 
                            break; 
                        }
                    }
                    if (x_count==1 && success) { 
                        if (is_fault_reach_[g->fis_[gnext]]) continue; 
                        obj.first = g->fis_[gnext]; 
                        obj.second = g->getInputCtrlValue(); 
                        event_list.push(obj); 
                    }
                }
            }
        }
/** 
        while (!event_list_forward.empty()) { 
            obj = event_list_forward.front(); 
            event_list_forward.pop(); 
        
            Value v = impl_->GetVal(obj.first); 
            if (v!=X) { 
                if (v==EvalNot(obj.second)) { return false; } 
                continue; 
            }
            if (!insertObj(obj, objs)) return false; 

            Gate *g = &cir_->gates_[obj.first]; 
            for (int i=0; i<g->nfo_; i++) {
                Gate *fo = &cir_->gates_[g->fos_[i]]; 
                if (fo->type_==Gate::BUF || fo->type_==Gate::INV 
                  || fo->type_==Gate::PO || fo->type_==Gate::PPO ) { 
                    obj.first = fo->id_; 
                    obj.second = 
                    (fo->isInverse())?EvalNot(obj.second):obj.second;
                    event_list_forward.push(obj); 
                }
                else { 
                    if (obj.second==fo->getInputCtrlValue()) { 
                        obj.first = fo->id_; 
                        obj.second = EvalNot(fo->getOutputCtrlValue()); 
                        event_list_forward.push(obj); 
                    }
                    else if (obj.second==fo->getInputNonCtrlValue()) { 
                        bool success = true; 
                        for (int i=0; i<fo->nfi_; i++) { 
                            int fi = fo->fis_[i]; 
                            if (fi==g->id_) continue; 
                            ObjListIter it = objs.find(fi);  
                            if (impl_->GetVal(fi)==fo->getInputNonCtrlValue() 
                              || (it!=objs.end() && it->second==fo->getInputNonCtrlValue())) continue; 
                            success = false; 
                            break; 
                        }
                        if (success) { 
                            obj.first = fo->id_; 
                            obj.second = fo->getOutputCtrlValue(); 
                            event_list_forward.push(obj); 
                        }
                    }  
                }
            }
        }
*/ 
        objs_ = objs; 
    }

    return true; 
}

void Atpg::CalcIsFaultReach(const GateVec &gv) { 
    stack<Gate *> events; 
    for (size_t i=0; i<gv.size(); i++)  
        events.push(gv[i]); 

    while (!events.empty()) { 
        Gate *g = events.top(); 
        events.pop(); 

        if (!CheckXPath(g)) continue; 
        is_fault_reach_[g->id_] = true; 
        for (int i=0; i<g->nfo_; i++) 
            if (!is_fault_reach_[g->fos_[i]]) 
                events.push(&cir_->gates_[g->fos_[i]]); 
    }
}

bool Atpg::GenObjs() { 
    GateVec gids; 
    size_t size; 
    bool ret = false; 

    objs_.clear(); 

    // get the previous object 
    d_tree_.top(gids); 
    Value *mask = d_tree_.top()->get_mask_(size); 
    CalcIsFaultReach(gids); 

    int j = 0; 
    for (size_t i=0; i<size; i++) { 
        if (mask[i]==L) continue; 
        Gate *gtoprop = gids[j++]; 
    
        if (!AddGateToProp(gtoprop)) { 
            if (mask[i]==H) return false; 
            mask[i] = L; 
        } 
        else { 
            mask[i] = H; 
            ret = true; 

            if (!objs_.empty()) { 
                // if has P/PIs obj. 
                if (objs_.begin()->first<cir_->npi_+cir_->nppi_) 
                    break; 
            }
        }
    }
    // assert(j==gids.size()); 

    if (!objs_.empty()) { 
        // if (!CheckDDDrive()) return false; 
        current_obj_ = *objs_.begin(); 
    }

    return ret; 
}

bool Atpg::CheckDDDrive() { 
    GateSet proped, pred; 
    d_tree_.top()->get_pred(pred); 
    d_tree_.sub_top()->top(proped); 

    return includes(pred.begin(), pred.end(), 
      proped.begin(), proped.end()); 
}

struct FaultPropEvent { 
    Gate   *event; 
    int     source; 

    FaultPropEvent(Gate *g, int s) { 
        event = g; 
        source = s; 
    }
}; 

void Atpg::PropFaultSet(const GateVec &gv, GateSetMap &pred) { 
    stack<FaultPropEvent> events; 
    
    // FaultSetMapIter it = f2p.begin(); 
    // for (; it!=f2p.end(); ++it) { 
    //     events.push(FaultPropEvent(it->first, it->first->id_)); 
    // }
    for (size_t i=0; i<gv.size(); i++) {
        events.push(FaultPropEvent(gv[i], gv[i]->id_)); 
    }

    while (!events.empty()) { 
        Gate *g = events.top().event; 
        int s = events.top().source; 
        events.pop(); 

        // it = f2p.find(g); 
        // FaultSet fs = it->second; 
        // AddFaultSet(g, fs); 

        Value v = impl_->GetVal(g->id_); 
        if (v==D || v==B) { 
            if (g->type_==Gate::PO || g->type_==Gate::PPO) { 
                GateSetMapIter itg = pred.find(g); 
                if (itg==pred.end()) { 
                    GateSet gs; gs.insert(s); 
                    pred.insert(pair<Gate *, GateSet>(g, gs)); 
                } 
                else { 
                    itg->second.insert(s); 
                }
                continue; 
            }

            for (int i=0; i<g->nfo_; i++) { 
                Gate *fo = &cir_->gates_[g->fos_[i]]; 
                // it = f2p.find(fo); 
                // if (it==f2p.end()) { 
                //     f2p.insert(pair<Gate *, FaultSet>(fo, fs)); 
                // } 
                // else { 
                //     it->second.insert(fs.begin(), fs.end()); // TODO 
                // }
                events.push(FaultPropEvent(fo, s)); 
            }
        }
        else if (v!=X) continue; 
        else { 
            GateSetMapIter itg = pred.find(g); 
            if (itg==pred.end()) { 
                GateSet gs; gs.insert(s); 
                pred.insert(pair<Gate *, GateSet>(g, gs)); 
            } 
            else { 
                itg->second.insert(s); 
            }
        }
    }
}

bool Atpg::MultiDDrive() { 
    GateVec dpath; 

    if (!GenObjs()) return false; 

    // Check path is sensitized 
    d_tree_.GetMultiPath(dpath); 
    if (CheckPath(dpath)) { 
        if (objs_.empty()) { // D-frontier pushed forward 
            GateVec dfront; 
            impl_->GetDFrontier(dfront); 
    
            if (!CheckDFrontier(dfront)) return false;

            // FaultSetMap f2p = d_tree_.top()->fault_to_prop_; 
            // FaultSetMap fp = d_tree_.top()->fault_proped_; 
            // GateSetMap pred; 
            // GateVec gids; d_tree_.top()->top(gids); 
            // PropFaultSet(gids, pred); 
    
            d_tree_.push(dfront, 
                impl_->GetEFrontierSize(), 
                impl_->getDecisionTree()); 
            impl_->ClearDecisionTree();  

            Value *mask = new Value [dfront.size()]; 
            for (size_t i=0; i<dfront.size(); i++) 
                mask[i] = X; 
            d_tree_.top()->set_mask_(mask); 
            // d_tree_.top()->fault_proped_ = fp; 
            // d_tree_.top()->set_f2p(f2p); 
            // d_tree_.top()->predecessor_ = pred; 

            GateVec &df = d_tree_.top()->dfront_; 
            sort (df.begin(), df.end(), comp_gate(this)); 

            if (!GenObjs()) return false; 
            else {
                if (objs_.empty()) { 
                    GateVec gids; d_tree_.top(gids); 
                    current_obj_.first = gids[0]->id_; 
                    current_obj_.second = gids[0]->getOutputCtrlValue(); 
                }
                return true; 
            } 
        } 
        else return true; // initial objective unchanged 
    }

    return false; // D-path justification failed 
}

bool Atpg::MultiDBackTrack(DecisionTree &tree) { 
    bool is_flipped = false; 
    bool ret = true; 
    size_t size; 
    Value *mask = d_tree_.top()->get_mask_(size); 
    while (size!=0) { 
        Value &v = mask[--size]; 
        if (v==H) { 
            if (!is_flipped) { 
                v = L; 
                is_flipped = true; 
            }
            else 
                ret = false; 
        }
        else { 
            if (!is_flipped) { 
                v = X; 
                ret = false; 
            }
        }
    }

    if (ret) 
        d_tree_.pop_hard(tree); 

    return ret; 
}

bool Atpg::isaMultiTest() { 
    GateVec gids; 

    // get the previous object 
    d_tree_.top(gids); 
    for (size_t i=0; i<gids.size(); i++) { 
        Gate *g = gids[i]; 

        if (!impl_->isGateDrivePpo(g)) return false; 
    } 

    /**  
    GateVec dfront; 
    FaultSetMap f2p = d_tree_.top()->fault_to_prop_; 
    FaultSetMap fp = d_tree_.top()->fault_proped_; 
    GateSetMap pred; 
    PropFaultSet(f2p, pred); 

    d_tree_.push(dfront, 
        impl_->GetEFrontierSize(), 
        impl_->getDecisionTree()); 

    d_tree_.top()->fault_proped_ = fp; 
    d_tree_.top()->set_f2p(f2p); 

    FaultSet fs; 
    d_tree_.top()->get_fs(fs); 
    prop_fs_ = fs.size(); 
    */ 

    return true; 
}

Fault *Atpg::GetFault(Gate *g, int line) { 
    Fault *f; int fid; 

    Value v; 
    v = (line>0)?impl_->GetVal(g->fis_[line-1]):impl_->GetVal(g->id_); 
    if (g->type_==Gate::PO || g->type_==Gate::PPO) 
        line--; 
    if (v!=D && v!=B) return 0; 
    else if (v==D) // SA0 
        fid = flist_->gateToFault_[g->id_] + 2 * line; 
    else if (v==B) // SA1  
        fid = flist_->gateToFault_[g->id_] + 2 * line + 1; 

    f = flist_->faults_[fid]; 

    if (f->state_==Fault::AB 
      || f->state_==Fault::AH 
      || f->state_==Fault::UD)  
        return f; 

    return 0; 
}

bool Atpg::comp_gate::operator()(Gate* g1, Gate* g2) {  
    /**
    FaultSetMap f2p = atpg_->d_tree_.top()->fault_to_prop_; 
    int fs1, fs2; 
    Value v1, v2; 

    for (int i=0; i<g1->nfi_; i++) { 
        Value v = atpg_->impl_->GetVal(g1->fis_[i]); 
        if (v==D || v==B) { 
            v1 = v; 
            break; 
        }
    }

    atpg_->ResetProbFaultSet(); 
    fs1 = f2p.find(g1)->second.size() + atpg_->GetProbFaultSet(g1, v1); 

    for (int i=0; i<g2->nfi_; i++) { 
        Value v = atpg_->impl_->GetVal(g2->fis_[i]); 
        if (v==D || v==B) { 
            v2 = v; 
            break; 
        }
    }

    atpg_->ResetProbFaultSet(); 
    fs2 = f2p.find(g2)->second.size() + atpg_->GetProbFaultSet(g2, v2); 
*/ 

    return g1->co_o_ > g2->co_o_; 
    // return fs1 > fs2; 
}

Fault *Atpg::GetProbFault(Gate *g, int line, Value vf) { 
    Fault *f; int fid; 

    Value v; 
    v = (line>0)?impl_->GetVal(g->fis_[line-1]):impl_->GetVal(g->id_); 

    if (g->type_==Gate::PO || g->type_==Gate::PPO) 
        line--; 

    if (v!=X) return 0; 
    else { 
        if (vf==D)
            fid = flist_->gateToFault_[g->id_] + 2 * line; // SA0 
        else if (vf==B)
            fid = flist_->gateToFault_[g->id_] + 2 * line + 1; // SA1  
        else 
            assert(0); 
    }

    f = flist_->faults_[fid]; 

    if (f->state_==Fault::AB 
      || f->state_==Fault::AH 
      || f->state_==Fault::UD)  
        return f; 

    return 0; 
} 

void Atpg::AddFaultSet(Gate *g, FaultSet &fs) { 
    Fault *f = GetFault(g, 0); 
    if (f) fs.insert(f); 

    if (current_fault_->gate_!=g->id_) { 
        for (int i=0; i<g->nfi_; i++) {
            Gate *fi = &cir_->gates_[g->fis_[i]]; 
            f = GetFault(g, i+1); 
            if (f) fs.insert(f); 
        } 
    } 
    else if (current_fault_->line_) { 
        fs.insert(current_fault_); 
    }
}

int Atpg::GetProbFaultSet(Gate *g, Value vi) { 

    if (prob_fs[g->id_]>=0) // gate visited 
        return prob_fs[g->id_]; 

    int ret = 0; 
    if (CheckXPath(g)) { 
        Value vo = (g->isInverse())?EvalNot(vi):vi;
        if (GetProbFault(g, 0, vo))  
            ret++; 
        for (int i=0; i<g->nfo_; i++) { 
            Gate *fo = &cir_->gates_[g->fos_[i]]; 
            for (int j=0; j<fo->nfi_; j++) { 
                if (fo->fis_[j]==g->id_) { 
                    if (GetProbFault(fo, j+1, vo))  
                        ret++; 
                    break; 
                }
            }
            ret+=GetProbFaultSet(fo, vo); 
        }
    }

    prob_fs[g->id_] = ret; 

    // if (current_fault_->gate_==g->id_) 
    //     prob_fs_ = ret; 

    return ret; 
}

