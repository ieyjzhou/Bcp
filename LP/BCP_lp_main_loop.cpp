// Copyright (C) 2000, International Business Machines
// Corporation and others.  All Rights Reserved.
#include <cstdio>
#include "BCP_timeout.hpp"
#include "BCP_message.hpp"
#include "BCP_error.hpp"
#include "BCP_lp_node.hpp"
#include "BCP_lp.hpp"
#include "BCP_lp_user.hpp"
#include "BCP_lp_functions.hpp"
#include "BCP_lp_result.hpp"
#include "BCP_warmstart.hpp"
#include "BCP_solution.hpp"

void BCP_lp_main_loop(BCP_lp_prob& p)
{
   BCP_lp_result& lpres = *p.lp_result;
   // argument flag for a number of functions. of course, here we invoke those
   // functions from the main loop, but this flag must be tru if the functions
   // are invoked from repricing. hence the flag is set here to false.
   const bool from_main_loop = false; 

   /*------------------------------------------------------------------------*
    * The main loop -- continue solving relaxations until no new cuts
    * are found
    *------------------------------------------------------------------------*/
   bool varset_changed = true;
   bool cutset_changed = true;
   double time0;

   if (p.param(BCP_lp_par::LpVerb_ProcessedNodeIndex))
      printf("\nLP: **** Processing NODE %i on LEVEL %i (from TM) ****\n",
	     p.node->index, p.node->level);
   // let the user do whatever she wants before the new node starts
   BCP_lp_prepare_for_new_node(p);

   while (true){
      ++p.node->iteration_count;

      BCP_lp_purge_slack_pool(p);

      if (p.param(BCP_lp_par::LpVerb_IterationCount))
	 printf("\nLP: *** Starting iteration %i ***\n",
		p.node->iteration_count);

      // Solve the lp relaxation and get the results
      time0 = BCP_time_since_epoch();
      BCP_lp_check_ub(p);
      p.user->modify_lp_parameters(p.lp_solver, false);
      p.lp_solver->resolve();
#if 0
      char fname[1000];
      sprintf(fname, "matrix-%i.%i.%i",
	      p.node->level, p.node->index, p.node->iteration_count);
      p.lp_solver->writeMps(fname, "mps");
#endif
      lpres.get_results(*p.lp_solver, false /* a pointer to the lp solver's
					       copy is fine */ );
      const BCP_termcode tc = lpres.termcode();
      p.stat.time_lp_solving += BCP_time_since_epoch() - time0;

      if (varset_changed) {
	 p.node->lb_at_cutgen.clear();
	 p.node->lb_at_cutgen.insert(p.node->lb_at_cutgen.end(),
				     p.node->cuts.size(), lpres.objval());
      }

      // Display the matrix solution value
      if (p.param(BCP_lp_par::LpVerb_LpSolutionValue)) {
	 printf("LP:   Matrix size: %i vars x %i cuts\n",
		static_cast<int>(p.node->vars.size()),
		static_cast<int>(p.node->cuts.size()));
	 printf("LP:   Solution value: %.4f / %i , %i \n",
		lpres.objval(), tc, lpres.iternum());
      }

      // Display the relaxed solution if needed
      if (p.param(BCP_lp_par::LpVerb_RelaxedSolution)) {
	p.user->display_lp_solution(lpres, p.node->vars, p.node->cuts, false);
      }
      
      // Test feasibility (note that it might be infeasible, but the user
      // might want to generate a heur feas sol anyway)
      time0 = BCP_time_since_epoch();
      BCP_lp_test_feasibility(p, lpres);
      p.stat.time_feas_testing += BCP_time_since_epoch() - time0;

      if (tc & BCP_ProvenPrimalInf) {
	 if (p.param(BCP_lp_par::LpVerb_FathomInfo))
	    printf("LP:   Primal feasibility lost.\n");
	 if (BCP_lp_fathom(p, from_main_loop)) {
	    BCP_lp_clean_up_node(p);
	    return;
	 }
	 varset_changed = true;
	 continue;
      }

      if (((tc & BCP_ProvenOptimal) && p.over_ub(lpres.objval())) ||
	  (tc & BCP_DualObjLimReached)) {
	 if (p.param(BCP_lp_par::LpVerb_FathomInfo))
	    printf("LP:   Terminating due to high cost.\n");
	 if (BCP_lp_fathom(p, from_main_loop)) {
	    BCP_lp_clean_up_node(p);
	    return;
	 }
	 varset_changed = true;
	 continue;
      }

      if (tc & (BCP_ProvenDualInf | BCP_PrimalObjLimReached | BCP_TimeLimit)) {
	 // *FIXME* : for now just throw an exception, but *THINK*
	 printf("LP: ############ Unexpected termcode: %i\n",lpres.termcode());
	 throw BCP_fatal_error("Unexpected termcode in BCP_lp_main_loop!\n");
      }

      if (tc & BCP_Abandoned) {
	 // *FIXME* : later we might want to prune this node, but continue
	 throw BCP_fatal_error("LP solver abandoned the lp.\n");
      }

      // We came here, therefore termcode must have been optimal and the
      // cost cannot be too high. Also, (at least so far) we haven't generated
      // any new variables.
      varset_changed = false;

      p.no_more_cuts_cnt = 0;
      p.no_more_vars_cnt = 0;
      if (! p.param(BCP_lp_par::MessagePassingIsSerial)) {
	 // If the message passing environment is really parallel (i.e., while
	 // the CG/CP are working we can do something else) then:
	 // send the current solution to CG, and also to CP if either
	 //  - lb_at_cutgen was reset, i.e., we are at the beginning of a
	 //    chain, or columns were generated. (This way we'll check the
	 //    pool at the top of the root, too, which is unnecessary, but it
	 //    doesn't hurt and no big time is lost.)
	 //  - or this is the cut_pool_check_freq-th iteration.
	 if (p.node->cg || p.node->cp) {
	    const BCP_message_tag msgtag = BCP_lp_pack_for_cg(p);
	    if (p.node->cg) {
	       ++p.no_more_cuts_cnt;
	       p.msg_env->send(p.node->cg, msgtag, p.msg_buf);
	    }
	    if (p.node->cp) {
	       if (! (p.node->iteration_count %
		      p.param(BCP_lp_par::CutPoolCheckFrequency))
		   || varset_changed) {
		  ++p.no_more_cuts_cnt;
		  p.msg_env->send(p.node->cp, msgtag, p.msg_buf);
	       }
	    }
	 }
	 // Similarly, send stuff to the VG/VP
	 if (p.node->vg || p.node->vp) {
	    const BCP_message_tag msgtag = BCP_lp_pack_for_vg(p);
	    if (p.node->vg) {
	       ++p.no_more_vars_cnt;
	       p.msg_env->send(p.node->vg, msgtag, p.msg_buf);
	    }
	    if (p.node->vp) {
	       if (! (p.node->iteration_count %
		      p.param(BCP_lp_par::VarPoolCheckFrequency))
		   || cutset_changed) {
		  ++p.no_more_vars_cnt;
		  p.msg_env->send(p.node->cp, msgtag, p.msg_buf);
	       }
	    }
	 }
      }

      // While CG and CP are working try to fix vars and test the
      // effectiveness of the rows.
      if (BCP_lp_fix_vars(p,
			  false /* not from fathom */,
			  true /* update_bd_change_count */)) {
	// during variable fixing primal feasibility is lost (must be due to
	// logical fixing by the user). Go back and resolve, but keep the same
	// iteration number
	--p.node->iteration_count;
	continue;
      }
      BCP_lp_adjust_row_effectiveness(p);

      // Generate and receive the cuts
      const int cuts_to_add_cnt =
	 BCP_lp_generate_cuts(p, varset_changed, from_main_loop);

      // Generate and receive the vars
      const int vars_to_add_cnt =
	 BCP_lp_generate_vars(p, cutset_changed, from_main_loop);

      time0 = BCP_time_since_epoch();
      BCP_solution* sol =
	p.user->generate_heuristic_solution(lpres, p.node->vars, p.node->cuts);
      p.stat.time_heuristics += BCP_time_since_epoch() - time0;

      if (sol != NULL) {
	p.user->send_feasible_solution(sol);
	delete sol;
	if (p.over_ub(lpres.objval())) {
	  if (p.param(BCP_lp_par::LpVerb_FathomInfo))
	    printf("LP:   Terminating due to high cost (good heur soln!).\n");
	  if (BCP_lp_fathom(p, from_main_loop)) {
	    BCP_lp_clean_up_node(p);
	    return;
	  }
	  varset_changed = true;
	  continue;
	}
      }

      const bool verb_cut = p.param(BCP_lp_par::LpVerb_GeneratedCutCount);
      const bool verb_var = p.param(BCP_lp_par::LpVerb_GeneratedVarCount);
      // Report how many have been generated
      if (verb_cut && ! verb_var) {
	 printf("LP:   In iteration %i BCP generated",p.node->iteration_count);
	 printf(" %i cuts before calling branch()\n", cuts_to_add_cnt);
      } else if (! verb_cut && verb_var) {
	 printf("LP:   In iteration %i BCP generated",p.node->iteration_count);
	 printf(" %i vars before calling branch()\n", vars_to_add_cnt);
      } else if (verb_cut && verb_var) {
	 printf("LP:   In iteration %i BCP generated",p.node->iteration_count);
	 printf(" %i cuts and %i vars before calling branch()\n",
		cuts_to_add_cnt, vars_to_add_cnt);
      }

      if (cuts_to_add_cnt == 0 && vars_to_add_cnt == 0 &&
	  p.param(BCP_lp_par::LpVerb_FinalRelaxedSolution)){
	// Display solution if nothing is generated
	p.user->display_lp_solution(lpres, p.node->vars, p.node->cuts, true);
      }

      // Try to branch
      switch (BCP_lp_branch(p)){
       case BCP_BranchingFathomedThisNode:
	 // Note that BCP_lp_branch() has already sent the node description to
	 // the TM.
	 if (p.param(BCP_lp_par::LpVerb_FathomInfo))
	    printf("LP:   Forcibly Pruning node\n");
	 // clean up
	 BCP_lp_clean_up_node(p);
	 return;

       case BCP_BranchingDivedIntoNewNode:
	 if (p.param(BCP_lp_par::LpVerb_ProcessedNodeIndex))
	    printf("\nLP: **** Processing NODE %i on LEVEL %i (dived) ****\n",
		   p.node->index, p.node->level);
	 // let the user do whatever she wants before the new node starts
	 BCP_lp_prepare_for_new_node(p);
	 // here we don't have to delete cols and rows, it's done as part of
	 // the cleanup during branching.
	 varset_changed = true;
	 cutset_changed = true;
	 break;

       case BCP_BranchingContinueThisNode:
	 // got to add things from the local pools
	 const int added_cuts = BCP_lp_add_from_local_cut_pool(p);
	 const int added_vars = BCP_lp_add_from_local_var_pool(p);
	 if (p.param(BCP_lp_par::LpVerb_AddedCutCount) &&
	     ! p.param(BCP_lp_par::LpVerb_AddedVarCount)) {
	    printf("LP:   In iteration %i BCP added %i cuts.\n",
		   p.node->iteration_count, added_cuts);
	 } else if (! p.param(BCP_lp_par::LpVerb_AddedCutCount) &&
		    p.param(BCP_lp_par::LpVerb_AddedVarCount)) {
	    printf("LP:   In iteration %i BCP added %i vars.\n",
		   p.node->iteration_count, added_vars);
	 } else if (p.param(BCP_lp_par::LpVerb_AddedCutCount) &&
		    p.param(BCP_lp_par::LpVerb_AddedVarCount)) {
	    printf("LP:   In iteration %i BCP added %i cuts and %i vars.\n",
		   p.node->iteration_count, added_cuts, added_vars);
	 }
	 varset_changed = (added_vars > 0);
	 cutset_changed = (added_cuts > 0);
	 // the args are: (p, col_indices, row_indices, force_delete).
	 // Here we don't have col/row_indices to compress and we don't want
	 // to force deletion.
	 BCP_lp_delete_cols_and_rows(p, 0, false);
	 break;
      }
   }
}
