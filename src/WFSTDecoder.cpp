/*
 * Copyright 2004 by IDIAP Research Institute
 *                   http://www.idiap.ch
 *
 * Copyright 2008 by The University of Sheffield
 *
 * Copyright 2008 by The University of Edinburgh
 *
 * See the file COPYING for the licence associated with this software.
 */

#include <assert.h>

#include "WFSTDecoder.h"
#include "DecHypHistPool.h"
#include "log_add.h"
#include "LogFile.h"

/*
    Author:  Darren Moore (moore@idiap.ch)
    Date:    14 October 2004
*/

/*
    Author:  Octavian Cheng (ocheng@idiap.ch)
    Date:    7 June 2006
*/

using namespace Torch;

namespace Juicer {

#define DEFAULT_DECHYPHISTPOOL_SIZE     5000
#define FRAMES_BETWEEN_LATTICE_PARTIAL_CLEANUP  2
#define FRAMES_BETWEEN_LATTICE_FULL_CLEANUP     10

WFSTDecoder::WFSTDecoder()
{
    mObjectName = "WFSTDecoder";
   network = NULL ;

   // Hypothesis Management
   decHypHistPool = new DecHypHistPool( DEFAULT_DECHYPHISTPOOL_SIZE ) ;
   modelPool = NULL ;
   auxReplaced = true;

   nActiveModels = 0 ;
   nActiveEmitHyps = 0 ;
   nActiveEndHyps = 0 ;
   nEmitHypsProcessed = 0 ;
   nEndHypsProcessed = 0 ;
   totalActiveModels = 0 ;
   totalActiveEmitHyps = 0 ;
   totalActiveEndHyps = 0 ;
   totalProcEmitHyps = 0 ;
   totalProcEndHyps = 0 ;
   avgActiveModels = 0.0 ;
   avgActiveEmitHyps = 0.0 ;
   avgActiveEndHyps = 0.0 ;
   avgProcEmitHyps = 0.0 ;
   avgProcEndHyps = 0.0 ;

   activeModelsList = NULL ;
   activeModelsLookup = NULL ;

   // Pruning
   emitPruneWin = -LOG_ZERO ;
   phoneEndPruneWin = -LOG_ZERO ;
   phoneStartPruneWin = -LOG_ZERO ;
   wordPruneWin = -LOG_ZERO ;
   maxEmitHyps = 0 ;

   bestEmitScore = LOG_ZERO ;
   currEmitPruneThresh = LOG_ZERO ;
#ifndef NO_BEST_END
   bestEndScore = LOG_ZERO ;
#endif
   currEndPruneThresh = LOG_ZERO ;
#ifndef NO_BEST_START
   bestStartScore = LOG_ZERO ;
#endif
   bestHypHist = NULL ;

   currWordPruneThresh = LOG_ZERO ;

   normaliseScore = 0.0 ;
   emitHypsHistogram = NULL ;

   models = NULL ;
   newActiveModelsList = NULL ;
   newActiveModelsListLastElem = NULL ;

   bestFinalHyp = NULL ;

   doModelLevelOutput = false ;

   nFrames = 0 ;
   currFrame = -1 ;

   // Lattice stuff
   doLatticeGeneration = false ;
   doLatticeDeadEndCleanup = false ;
   lattice = NULL ;
}


WFSTDecoder::WFSTDecoder(
    WFSTNetwork *network_ , IModels *models_ ,
    real phoneStartPruneWin_ , real emitPruneWin_ ,  real phoneEndPruneWin_ , real wordPruneWin_ ,
    int maxEmitHyps_ , bool doModelLevelOutput_ ,
    bool doLatticeGeneration_ , bool isStaticComposition_
)
{
    mObjectName = "WFSTDecoder";

   int i ;
    LogFile::printf("WFSTDecoder initialised with:\n");
    LogFile::printf("\tmaxEmitHyps = %d\n",maxEmitHyps);
    emitPruneWin_ == LOG_ZERO ? LogFile::printf("\temitPruneWin = LOG_ZERO\n"):LogFile::printf("\temitPruneWin = %f\n", emitPruneWin_);
    phoneStartPruneWin_ == LOG_ZERO ? LogFile::printf("\tphoneStartPruneWin = LOG_ZERO\n"):LogFile::printf("\tphoneStartPruneWin = %f\n", phoneStartPruneWin_);
    phoneEndPruneWin_ == LOG_ZERO ? LogFile::printf("\tphoneEndPruneWin = LOG_ZERO\n"):LogFile::printf("\tphoneEndPruneWin = %f\n", phoneEndPruneWin_);
    wordPruneWin_ == LOG_ZERO ? LogFile::printf("\twordPruneWin = LOG_ZERO\n"):LogFile::printf("\twordPruneWin = %f\n", wordPruneWin_);
    
   if ( (network = network_) == NULL )
      error("WFSTDecoder::WFSTDecoder(2) - network_ is NULL") ;
   if ( (models = models_) == NULL )
      error("WFSTDecoder::WFSTDecoder(2) - models is NULL") ;

   currFrame = -1 ;

   // Hypothesis Management
   decHypHistPool = new DecHypHistPool( DEFAULT_DECHYPHISTPOOL_SIZE ) ;
   auxReplaced = true;

   if ( isStaticComposition_ )
      modelPool = new WFSTModelPool( models , decHypHistPool ) ;
   else
      modelPool = NULL ;

   nActiveModels = 0 ;
   nActiveEmitHyps = 0 ;
   nActiveEndHyps = 0 ;
   nEmitHypsProcessed = 0 ;
   nEndHypsProcessed = 0 ;
   totalActiveModels = 0 ;
   totalActiveEmitHyps = 0 ;
   totalActiveEndHyps = 0 ;
   totalProcEmitHyps = 0 ;
   totalProcEndHyps = 0 ;
   avgActiveModels = 0.0 ;
   avgActiveEmitHyps = 0.0 ;
   avgActiveEndHyps = 0.0 ;
   avgProcEmitHyps = 0.0 ;
   avgProcEndHyps = 0.0 ;

   activeModelsList = NULL ;
   activeModelsLookupLen = 0 ;
   activeModelsLookup = NULL ;
      activeModelsLookupLen = network->getNumTransitions() ;

      //printf("activeModelsLookupLen = %d\n", activeModelsLookupLen);

   if ( isStaticComposition_ )  {
       activeModelsLookup = (WFSTModel **)malloc(
           activeModelsLookupLen * sizeof(WFSTModel *)
       ) ;
       for ( i=0 ; i<activeModelsLookupLen ; i++ )
           activeModelsLookup[i] = NULL ;
   }
   else  {
       activeModelsLookup = NULL ;
   }

   // Pruning
   if ( (emitPruneWin = emitPruneWin_) <= 0.0 )
       emitPruneWin = -LOG_ZERO ;
   if ( (phoneEndPruneWin = phoneEndPruneWin_) <= 0.0 )
       phoneEndPruneWin = -LOG_ZERO ;
   if ( (phoneStartPruneWin = phoneStartPruneWin_) <= 0.0 )
       phoneStartPruneWin = -LOG_ZERO ;
   if ( (wordPruneWin = wordPruneWin_) <= 0.0 )
       wordPruneWin = -LOG_ZERO ;
   if ( (maxEmitHyps = maxEmitHyps_) < 0 )
       maxEmitHyps = 0 ;

   bestEmitScore = LOG_ZERO ;
   currEmitPruneThresh = LOG_ZERO ;
#ifndef NO_BEST_END
   bestEndScore = LOG_ZERO ;
#endif
   currEndPruneThresh = LOG_ZERO ;
#ifndef NO_BEST_START
   bestStartScore = LOG_ZERO ;
#endif
   bestHypHist = NULL ;

   currWordPruneThresh = LOG_ZERO ;

   normaliseScore = 0.0 ;
   emitHypsHistogram = NULL ;
   if ( maxEmitHyps > 0 )
   {
       if ( emitPruneWin < (-LOG_ZERO) )
           emitHypsHistogram = new Histogram(
               1 , (-emitPruneWin) - 800.0  , 200.0
           ) ;
       else
           emitHypsHistogram = new Histogram( 1 , -1000.0 , 200.0 ) ;
   }

   newActiveModelsList = NULL ;
   newActiveModelsListLastElem = NULL ;

   if ( isStaticComposition_ )  {
      bestFinalHyp = new DecHyp() ;
      DecHypHistPool::initDecHyp( bestFinalHyp , -1 ) ;
   }
   else  {
      bestFinalHyp = NULL ;
   }

   doModelLevelOutput = doModelLevelOutput_ ;

   nFrames = 0 ;
   currFrame = -1 ;

   // Lattice stuff
   doLatticeGeneration = doLatticeGeneration_ ;
   doLatticeDeadEndCleanup = true ;
   lattice = NULL ;
   if ( doLatticeGeneration )
   {
      lattice = new WFSTLattice( network->getNumStates() , false , false ) ;
      lattice->enableDeadEndRemoval( FRAMES_BETWEEN_LATTICE_PARTIAL_CLEANUP ,
                                     FRAMES_BETWEEN_LATTICE_FULL_CLEANUP ) ;
      decHypHistPool->setLattice( lattice ) ;
   }

   //printf("DecHyp is %u bytes\n", sizeof(DecHyp));
}


WFSTDecoder::~WFSTDecoder() throw ()
{
    reset() ;

    delete bestFinalHyp ;
    delete emitHypsHistogram ;

    if ( activeModelsLookup != NULL )
        free( activeModelsLookup ) ;

    delete modelPool ;
    delete decHypHistPool ;
    delete lattice ;
}

void WFSTDecoder::init()
{
    DecHypHistPool::initDecHyp( bestFinalHyp , -1 ) ;

    reset() ;

    // Reset the time
    currFrame = 0 ;

   // initialise a starting hypothesis.
   DecHyp tmpHyp ;
   DecHypHistPool::initDecHyp( &tmpHyp, -1 );
   tmpHyp.score = 0.0 ;
   tmpHyp.acousticScore = 0.0 ;
   tmpHyp.lmScore = 0.0 ;

   // Extend the hypothesis into the initial states of the models
   //   associated with the transitions out of the initial state.
   extendModelEndState( &tmpHyp , NULL) ;

   joinNewActiveModelsList() ;

   resetDecHyp( &tmpHyp ) ;
}


void WFSTDecoder::processFrame( real **inputVec, int currFrame_, int nFrames_)
{
    // PNG - This could be maintained by the decoder alone rather than
    // the calling routine, but for now this is OK as it eases the
    // translation to real-time.
    // printf("process frame %d\n", currFrame_); fflush(stdout);
    currFrame = currFrame_;
    nFrames++;

   // Reset the bestFinalHyp
   resetDecHyp( bestFinalHyp ) ;

#ifdef NO_BEST_START
   currStartPruneThresh = bestEmitScore - phoneStartPruneWin ;
#else
   currStartPruneThresh = bestStartScore - phoneStartPruneWin ;
#endif

   // Process the hypotheses in the initial states of all active models.
   processActiveModelsInitStates() ;

    // Now all hypotheses have come to rest in emitting states, ready to
    //   process the new frame.

    // Inform the phoneModels/models of the new input vector
   models->newFrame(currFrame, inputVec, nFrames_); 

   if ( doLatticeGeneration )
   {
      // Inform the lattice of the new frame
      lattice->newFrame( currFrame ) ;
   }

    // Calculate the new normalisation factor and emitting state pruning threshold.
    if ( bestEmitScore <= LOG_ZERO )
        normaliseScore = 0.0 ;
    else
        normaliseScore = bestEmitScore ;

    if ( emitHypsHistogram != NULL )
    {
        currEmitPruneThresh = emitHypsHistogram->calcThresh( maxEmitHyps ) ;
        currEmitPruneThresh -= normaliseScore ;
        emitHypsHistogram->reset() ;
        if ( currEmitPruneThresh < -emitPruneWin )
            currEmitPruneThresh = -emitPruneWin ;
    }
    else
        currEmitPruneThresh = -emitPruneWin ;

//printf( "bestEmitScore = %6f normaliseScore = %6f currEmitThresh = %6f\n" ,
//        bestEmitScore , normaliseScore , currEmitPruneThresh ) ;

    // Process emitting states for the new frame and calculate the new phone-end
    //   pruning threshold.
    processActiveModelsEmitStates() ;
#ifdef NO_BEST_END
    currEndPruneThresh = bestEmitScore - phoneEndPruneWin ;
    currWordPruneThresh = bestEmitScore - wordPruneWin ;
#else
    currEndPruneThresh = bestEndScore - phoneEndPruneWin ;
    currWordPruneThresh = bestEndScore - wordPruneWin ;
#endif


   totalActiveEmitHyps += nActiveEmitHyps ;
   totalActiveEndHyps += nActiveEndHyps ;
   totalProcEmitHyps += nEmitHypsProcessed ;

   //printf("PHN(%d): nActPhn=%d nActiveEmit=%d nActiveEnd=%d nEmitProc=%d ",currFrame,nActiveModels,nActiveEmitHyps,nActiveEndHyps,nEmitHypsProcessed);fflush(stdout);

   //printf( "bestEndScore = %6f normaliseScore = %6f currEndThresh = %6f\n" ,
   //         bestEndScore , normaliseScore , currEndPruneThresh ) ;

    // Process phone-end hyps and calculate the new pronun-end pruning threshold
    processActiveModelsEndStates() ;
    totalProcEndHyps += nEndHypsProcessed ;
    totalActiveModels += nActiveModels ;
//printf("nEndProc=%d\n",nEndHypsProcessed);fflush(stdout);

//lattice->printLogInfo() ;

#if 0
   printf("ACTIVE: %d %d %d %d %f\n",
          currFrame, nActiveModels, nActiveEmitHyps, nActiveEndHyps,
          -bestFinalHyp->score);
#endif
}


void WFSTDecoder::processActiveModelsInitStates()
{
   WFSTModel *model = activeModelsList ;
   WFSTModel *prevModel = NULL ;

   while ( model != NULL )
   {
       DecHyp *hyp ;
       hyp = model->currHyps ;
       if ( hyp->score > LOG_ZERO )
       {
          // Language model pruning
          if (hyp->score > currStartPruneThresh)
          {
              extendModelInitState( model ) ;
          }
          else
          {
              // Prune this hypothesis right now
              resetDecHyp( model->currHyps ) ;
              --(model->nActiveHyps);
          }
      }

      // Next model, which could involve deactivating this model
      if (model->nActiveHyps)
      {
          prevModel = model;
          model = model->next ;
      }
      else
          model = returnModel(model, prevModel);

   }
}


void WFSTDecoder::extendModelInitState( WFSTModel *model )
{
#ifdef DEBUG
    if ( DecHypHistPool::isActiveHyp( model->currHyps ) == false )
        error("WFSTDecoder::extendModelInitSt - no active hyp in init state") ;
#endif

    DecHyp *currHyps = model->currHyps ;

    // Evaluate transitions from the initial state to successor states.
    int finalState = models->getNumStates(model->hmmIndex) - 1;
    int nSucs = models->getNumSuccessors(model->hmmIndex, 0);
    for ( int i=0 ; i<nSucs ; i++ )
    {
        int sucInd = models->getSuccessor(model->hmmIndex, 0, i) ;
        if (sucInd == finalState)
        {
            // Ignore tee transitions.  They are dealt with later
            continue;
        }
        real sucProb = models->getSuccessorLogProb(model->hmmIndex, 0, i);
        real newScore = currHyps[0].score + sucProb;
        real acousticScore = currHyps[0].acousticScore + sucProb ;
        real oldScore = currHyps[sucInd].score ;

        if ( newScore > oldScore )
        {
            bool emittingState = (sucInd != 0) && (sucInd != finalState);

            // If the hypothesis for the successor state is being
            // activated for the first time, increment the number of
            // active hypotheses.
            if ( oldScore <= LOG_ZERO )
            {
                model->nActiveHyps++ ;
                if ( emittingState )
                    nActiveEmitHyps++ ;
                else
                {
                    // Should not get here
                    assert(0);
                    error("WFSTDecoder::extendModelInitSt - "
                          "tee models not supported at the moment") ;
                }
            }

            // The new hypothesis is better than the one that
            //   is already at this successor state. Replace.
            decHypHistPool->extendDecHyp(
                currHyps , currHyps+sucInd , newScore ,
                acousticScore , currHyps[0].lmScore
            ) ;

            if ( (emitHypsHistogram != NULL) && emittingState )
            {
                // Add the new score (and perhaps remove the old) from
                // the emitting state hypothesis scores histogram.
                emitHypsHistogram->addScore( newScore , oldScore ) ;
            }

            // Update our best emitting state scores and if the
            // successor state was a phone end then process that now.
            if ( emittingState && (newScore > bestEmitScore) )
            {
                bestEmitScore = newScore ;
                bestHypHist = currHyps[sucInd].hist ;
            }
            else if (!emittingState)
            {
                error("WFSTDecoder::extendModelInitSt"
                      " - tee models not supported at the moment") ;
            }
        }
    }

    // Deactivate the initial state and update number of active hyps
    // count in model.
    resetDecHyp( currHyps ) ;
    --(model->nActiveHyps) ;
}


/**
 * For each phone in the activeModelsList, update the emitting state
 * hypotheses for the new frame.
 */
void WFSTDecoder::processActiveModelsEmitStates()
{
#ifdef DEBUG
    checkActiveNumbers( false ) ;
#endif

    // Reset all of the (per-frame) active counts and best scores
    nActiveEmitHyps = 0 ;
    nActiveEndHyps = 0 ;
    nEmitHypsProcessed = 0 ;
    nEndHypsProcessed = 0 ;
    bestEmitScore = LOG_ZERO ;
#ifndef NO_BEST_END
    bestEndScore = LOG_ZERO ;
#endif
    bestHypHist = NULL ;

#ifdef DEBUG
    // Make sure 'frame' is in sync with phoneModels/models
    if ( currFrame != models->getCurrFrame() )
        error("WFSTDecoder::procActModEmitSts - "
              "currFrame != models->getCurrFrame()") ;
#endif

   WFSTModel *model = activeModelsList ;
   WFSTModel *prevModel = NULL ;
   while ( model != NULL )
   {
      processModelEmitStates( model ) ;

      // Check to see if pruning resulted in no emitting
      //   states being extended.
      if ( model->nActiveHyps <= 0 )
      {
         model = returnModel( model , prevModel ) ;
      }
      else
      {
         prevModel = model ;
         model = model->next ;
      }
   }

#ifdef DEBUG
    checkActiveNumbers( false ) ;
#endif
}


void WFSTDecoder::processModelEmitStates( WFSTModel *model )
{
    int i ;
    int nStates = models->getNumStates(model->hmmIndex);
    int finalState = models->getNumStates(model->hmmIndex) - 1;

    // Flip the prevHyps and currHyps.
    DecHyp *prevHyps = model->currHyps ;
    DecHyp *currHyps = model->prevHyps ;
    model->currHyps = currHyps ;
    model->prevHyps = prevHyps ;

#ifdef DEBUG
    // All hyps in the 'currHyps' field of this WFSTModel element
    // should be inactive - check
    for ( i=0 ; i<nStates ; i++ )
    {
        if ( DecHypHistPool::isActiveHyp( currHyps + i ) )
            error("WFSTDecoder::procModelEmitSts - "
                  "currHyps element is still active") ;
    }
    // The number of active emitting state hyps in 'prevHyps' should
    // equal the nActiveHyps field of model
    int cnt=0 ;
    for ( i=0 ; i<nStates ; i++ )
    {
        if ( DecHypHistPool::isActiveHyp( prevHyps + i ) )
            cnt++ ;
    }
    if ( cnt != model->nActiveHyps )
    {
        error("WFSTDecoder::procModelEmitSts - nActiveHyps incorrect %d %d" ,
              cnt , model->nActiveHyps ) ;
    }
    // The initial and final state hyps in 'prevHyps' should be
    // inactive
    if ( DecHypHistPool::isActiveHyp( prevHyps ) )
        error("WFSTDecoder::procModelEmitSts - "
              "prevHyps init st hyp is still active") ;
    if ( DecHypHistPool::isActiveHyp( prevHyps + (nStates-1) ) )
        error("WFSTDecoder::procModelEmitSts - "
              "prevHyps final st hyp is still active") ;
#endif

    // Reset the count of active hyps
    model->nActiveHyps = 0 ;

    // Update the emitting states in currPhone
    int nEmitStates = nStates-1;
    for ( i=1 ; i<nEmitStates ; i++ )
    {
        // Look at each emitting state hypothesis in this phone model.
        // Update if active.
        DecHyp &prevHyp_i = prevHyps[i];
        if ( prevHyp_i.score > LOG_ZERO )
        {
            // We have an active hypothesis.  Normalise this score
            // using the best hypothesis score from the previous frame
            prevHyp_i.score -= normaliseScore ;

            // Check if it is within the current emitting state pruning beam.
            if ( prevHyp_i.score > currEmitPruneThresh )
            {
                nEmitHypsProcessed++ ;

                // 1. Calculate its emission probability.
                real emisProb ;
                emisProb = models->calcOutput( model->hmmIndex, i ) ;

                // 2. Evaluate transitions to successor states.
                int nSucs = models->getNumSuccessors(model->hmmIndex, i);
                for ( int j=0 ; j<nSucs ; j++ )
                {
                    // Calculate the (potential) new scores for the
                    // successor state.
                    int sucInd = models->getSuccessor(model->hmmIndex, i, j);
                    real sucProb =
                        models->getSuccessorLogProb(model->hmmIndex, i, j);
                    //printf("sucInd = %d\n", sucInd);
                    real newScore =
                        prevHyp_i.score + emisProb + sucProb ;
                    real oldScore = currHyps[sucInd].score ;

                    if ( newScore > oldScore )
                    {
                        real acousticScore =
                            prevHyp_i.acousticScore
                            + emisProb + sucProb ;

                        assert(sucInd != 0);
                        bool emittingState =
                            (sucInd != 0) && (sucInd != finalState);
                        // If the hypothesis for the successor state
                        // is being activated for the first time,
                        // increment the number of active hypotheses.
                        if ( oldScore <= LOG_ZERO )
                        {
                            model->nActiveHyps++ ;
                            if ( emittingState )
                                nActiveEmitHyps++ ;
                            else
                                nActiveEndHyps++ ;
                        }

                        if ( (emitHypsHistogram != NULL) && emittingState )
                        {
                            // Add the new score (and perhaps remove
                            // the old) from the emitting state
                            // hypothesis scores histogram.
                            emitHypsHistogram->addScore( newScore , oldScore );
                        }

                        // The new hypothesis is better than the one
                        // that is already at this successor
                        // state. Replace.
                        decHypHistPool->extendDecHyp(
                            prevHyps+i , currHyps+sucInd , newScore ,
                            acousticScore , prevHyp_i.lmScore
                        ) ;

                        // Update our best scores.
                        if ( emittingState && (newScore > bestEmitScore) )
                        {
                            bestEmitScore = newScore ;
                            bestHypHist = currHyps[sucInd].hist ;
                        }
                        else
#ifdef NO_BEST_END
                            if ( !emittingState && (newScore > bestEmitScore) )
                                bestEmitScore = newScore ;
#else
                            if ( !emittingState && (newScore > bestEndScore) )
                                bestEndScore = newScore ;
#endif
                    }
                }
            }

            // We've finished processing this state - deactivate.
             resetDecHyp(&prevHyp_i ) ;
        }
    }

#ifdef DEBUG
    // Test - check that all hyps in prevHyps are now deactivated
    for ( i=0 ; i<nStates ; i++ )
    {
        if ( DecHypHistPool::isActiveHyp( prevHyps + i ) )
            error("WFSTDecoder::procModelEmitSts - "
                  "prevHyps[%d] not deactivated", i) ;
    }
    // Check that the number of active hyps in currHyps equals
    // currPhone->nActiveHyps
    int cnt2=0 ;
    for ( i=0 ; i<nStates ; i++ )
    {
        if ( DecHypHistPool::isActiveHyp( currHyps + i ) )
            ++cnt2 ;
    }
    if ( cnt2 != model->nActiveHyps )
    {
        error("WFSTDecoder::procModelEmitSts - "
              "nActiveHyps incorrect at end. %d %d" ,
              cnt2 , model->nActiveHyps ) ;
    }
#endif
}



/**
 * Look at the final state hypothesis for all models in the active
 * list.  If the end state is active, then we extend it to successor
 * models as defined by the network.
 */
void WFSTDecoder::processActiveModelsEndStates()
{
#ifdef DEBUG
    checkActiveNumbers( false ) ;
#endif

    // If we don't have any phone end hyps we don't need to go any further.
    if ( nActiveEndHyps <= 0 )
        return ;

#ifndef NO_BEST_START
    bestStartScore = LOG_ZERO ;
#endif

    WFSTModel *model = activeModelsList ;
    WFSTModel *prevModel = NULL ;
    while ( model != NULL )
    {
        // zl: nStates need to be updated for each model (as sp can
        // only has 3 states not 5 states for example)
        int nStates = models->getNumStates(model->hmmIndex);
        DecHyp *endHyp = model->currHyps + (nStates - 1) ;
        if ( endHyp->score > LOG_ZERO )
        {
            // VW - word based pruning
            // Use a different purning threshold at word ends
            if (model->trans->outLabel != WFST_EPSILON) 
            {
                if ( endHyp->score > currWordPruneThresh )
                {
                    // Extend hypothesis to new models.
                    nEndHypsProcessed++ ;
                    extendModelEndState( endHyp , model->trans) ;
                }
            }
            else
            {
                if ( endHyp->score > currEndPruneThresh )
                {
                    // Extend hypothesis to new models.
                    nEndHypsProcessed++ ;
                    extendModelEndState( endHyp , model->trans) ;
                }
            }

            // Deactivate endHyp
            resetDecHyp( endHyp ) ;
            if ( (--nActiveEndHyps) < 0 )
                error("WFSTDecoder::processActiveModelsEndHyps"
                        " - nActiveEndHyps < 0") ;

            // Update the number of active hypotheses in model.
            // Deactivate model if no active hypotheses remain.
            if ( --(model->nActiveHyps) == 0 )
            {
                model = returnModel( model , prevModel ) ;
            }
            else
            {
                prevModel = model ;
                model = model->next ;
            }
        }
        else
        {
            prevModel = model ;
            model = model->next ;
        }
    }

    // We have probably extended into new models that were not
    // previously in the activeModelsList.  These models are currently
    // being held in the newActiveModelsList.  Join this new list to
    // the front of the activeModelsList, to form a new
    // activeModelsList
    joinNewActiveModelsList() ;

#ifdef DEBUG
    checkActiveNumbers( false ) ;
#endif
}

void WFSTDecoder::extendModelEndState( DecHyp *endHyp , WFSTTransition *trans)
{
#ifdef DEBUG
    if ( endHyp == NULL )
        error("WFSTDecoder::extendModelEndState - endHyp == NULL") ;
    if ( endHyp->score <= LOG_ZERO )
        error("WFSTDecoder::extendModelEndState - score <= LOG_ZERO") ;
    if ( endHyp->score <= currEndPruneThresh )
        error("WFSTDecoder::extendModelEndState"
              " - score %e <= currEndPruneThresh %e",
              endHyp->score, currEndPruneThresh);
#endif

    int lattToState=-1 , lattFromState=-1 ;

    if ( trans != NULL )
    {
#ifdef DEBUG
        if ( trans->outLabel == network->getWordEndMarker() )
            error("WFSTDecoder::extendModelEndState"
                  " - out label is word-end marker");
#endif

        if ( doLatticeGeneration )
        {
            lattToState = addLatticeEntry( endHyp , trans , &lattFromState ) ;
        }

        if ( doModelLevelOutput )
        {
            if ( trans->outLabel != WFST_EPSILON )
            {
                addLabelHist( endHyp, trans->outLabel ) ;
            }

            if ( (trans->inLabel != WFST_EPSILON) &&
                 (trans->inLabel != network->getWordEndMarker()) )
            {
                decHypHistPool->addHistToDecHyp(
                    endHyp , trans->inLabel , endHyp->score ,
                    currFrame , endHyp->acousticScore , endHyp->lmScore
                );
            }
        }
        else
        {

            if ( trans->outLabel != WFST_EPSILON )
            {
                registerLabel( endHyp, trans->outLabel ) ;
                if (auxReplaced)
                    // PNG - Immediately register the end as there are no
                    // word end labels.
                    decHypHistPool->registerEnd(
                        endHyp, endHyp->score, currFrame,
                        endHyp->acousticScore, endHyp->lmScore
                    );
            }

            if (!auxReplaced)
                if ( trans->inLabel == network->getWordEndMarker() )
                {
                    decHypHistPool->registerEnd(
                        endHyp, endHyp->score, currFrame,
                        endHyp->acousticScore, endHyp->lmScore
                    );
                }
        }
#ifdef DEBUG
        if ( doLatticeGeneration )
        {
            if ( (endHyp->hist != NULL) &&
                 (endHyp->hist->type == LATTICEDHHTYPE) )
                error("WFSTDecoder::extendModelEndState"
                      " - unexpected LATTICEDHHTYPE history found") ;
        }
#endif

        // If this transition goes to a final state, then check to see
        // if this is the best final state hypothesis.
        if ( network->transGoesToFinalState( trans ) )
        {
            real fScore = network->getFinalStateWeight( trans ) ;
            if ( doLatticeGeneration )
            {
                lattice->addFinalState( lattToState , fScore ) ;
            }

            // But first add on the final state weight.
            if ( (endHyp->score + fScore) > bestFinalHyp->score )
            {
                decHypHistPool->extendDecHyp(
                    endHyp , bestFinalHyp , endHyp->score+fScore ,
                    endHyp->acousticScore , endHyp->lmScore+fScore
                ) ;
            }
        }

        if ( doLatticeGeneration )
        {
            // Add lattice history to this end hyp, for quick access
            // to the previous lattice state, as well as the score.
            decHypHistPool->addLatticeHistToDecHyp(
                endHyp , lattToState ,
                endHyp->acousticScore + endHyp->lmScore
            ) ;

            // Register that there is a potential transition going out
            // from lattToState.  This is so that there is correct
            // behaviour when endHyp gets deactivated after being
            // extended to init states.
            lattice->registerActiveTrans( lattToState ) ;
        }
    }
    else
    {
#ifdef DEBUG
        if ( currFrame != 0 )
            error("WFSTDecoder::extModelEndState"
                  " - currFrame==0 when trans==NULL assumption wrong") ;
#endif

        if ( doLatticeGeneration )
        {
            // Add lattice history to this end hyp, for quick access
            // to the previous lattice state, as well as the score.
            decHypHistPool->addLatticeHistToDecHyp(
                endHyp, lattice->getInitState(), endHyp->score
            ) ;

            // Register that there is a potential transition going out
            // from lattToState.  This is so that there is correct
            // behaviour when endHyp gets deactivated after being
            // extended to init states.
            lattice->registerActiveTrans( lattice->getInitState() ) ;
        }
    }

    // Retrieve the next transitions for the current model
    int nNextTrans=0 ;
    WFSTTransition* nextTrans;
    nNextTrans = network->getTransitions(trans, &nextTrans);
    for ( int i=0 ; i<nNextTrans ; i++ )
    {
        if ( (nextTrans[i].inLabel != WFST_EPSILON) &&
             (nextTrans[i].inLabel != network->getWordEndMarker()) )
        {
            // There is a model associated with the next transition
            WFSTModel *nextModel = activeModelsLookup[nextTrans[i].id];
            if (nextModel == NULL)
                nextModel = getNewModel(&nextTrans[i]) ;
            real newScore = endHyp->score + nextTrans[i].weight ;

            if ( newScore > nextModel->currHyps[0].score )
            {
                // Extend this hypothesis into the entry state of the new model
                if ( nextModel->currHyps[0].score <= LOG_ZERO )
                    nextModel->nActiveHyps++ ;

                decHypHistPool->extendDecHyp(
                    endHyp , nextModel->currHyps , newScore,
                    endHyp->acousticScore,
                    (endHyp->lmScore + nextTrans[i].weight)
                ) ;

#ifdef NO_BEST_START
                if (newScore > bestEmitScore)
                    bestEmitScore = newScore;
#else
                if (newScore > bestStartScore)
                    bestStartScore = newScore;
#endif

            }

            // In addition, there might be a tee transition to be passed on
            real teeWeight = models->getTeeLogProb(nextModel->hmmIndex);
            if (teeWeight > LOG_ZERO)
            {
                // zl: tee model is supposed to be passed directly and it should not 
                // be extended to the final state of nextModel
                newScore += teeWeight;
                DecHyp tmpHyp ;
                DecHypHistPool::initDecHyp( &tmpHyp, -1 );
                decHypHistPool->extendDecHyp(endHyp,
                        &tmpHyp,
                        newScore,
                        endHyp->acousticScore + teeWeight,
                        endHyp->lmScore + nextTrans[i].weight);


                // VW - word based pruning
                // Use a different purning threshold at word ends
                if (nextModel->trans->outLabel != WFST_EPSILON) 
                {
                    if ( newScore > currWordPruneThresh )
                    {
                        extendModelEndState( &tmpHyp, &nextTrans[i]) ;
                    }
                }
                else
                {
                    if (newScore > currEndPruneThresh)
                    {
                        extendModelEndState(&tmpHyp, &nextTrans[i]) ;
                    }
                }
            } // end of teeWeight
        }
        else
        {
            // Epsilon transitions
            real epsWeight = nextTrans[i].weight;

            // There is no input (ie. model) associated with this
            // transition.  There could possibly be an output label
            // though.  Extend current end hypothesis to a temp
            // hypothesis and then recursively call this function to
            // extend temp hypothesis into transitions that follow the
            // one with epsilon input label.
            DecHyp tmp ;
            DecHypHistPool::initDecHyp( &tmp, -1 );
            decHypHistPool->extendDecHyp(
                endHyp , &tmp ,
                endHyp->score + epsWeight,
                endHyp->acousticScore,
                endHyp->lmScore + epsWeight
            ) ;

            if (tmp.score > currEndPruneThresh)
            {
                extendModelEndState( &tmp , &nextTrans[i]) ;
            }

            // Reset the temp hypothesis before it goes out of scope.
            resetDecHyp( &tmp ) ;
        }
    }
}

DecHyp *WFSTDecoder::finish()
{
    // Statistics
    avgActiveEmitHyps = (real)totalActiveEmitHyps / (real)nFrames ;
    avgActiveEndHyps = (real)totalActiveEndHyps / (real)nFrames ;
    avgActiveModels = (real)totalActiveModels / (real)nFrames ;
    avgProcEmitHyps = (real)totalProcEmitHyps / (real)nFrames ;
    avgProcEndHyps = (real)totalProcEndHyps / (real)nFrames ;

    LogFile::printf(
        "\nStatistics:\n  nFrames=%d\n  avgActiveEmitHyps=%.2f\n"
        "  avgActiveEndHyps=%.2f\n  avgActiveModels=%.2f\n"
        "  avgProcessedEmitHyps=%.2f\n  avgProcessedEndHyps=%.2f\n" ,
        nFrames , avgActiveEmitHyps , avgActiveEndHyps , avgActiveModels ,
        avgProcEmitHyps , avgProcEndHyps
    ) ;

    // Deactivate all active hypotheses (except bestFinalHyp of course)
    resetActiveHyps() ;

    if ( doLatticeGeneration )
    {
        lattice->removeDeadEndTransitions( true ) ;
        lattice->removeDeadEndTransitions( true ) ;
        lattice->printLogInfo() ;
    }

    if ( DecHypHistPool::isActiveHyp( bestFinalHyp ) )
    {
        // If labels are remaining to be written, something is wrong
        if ( bestFinalHyp->nLabelsNR > 0 )
            //error("WFSTDecoder::finish - bestFinalHyp.nLabelsNR > 0");
            LogFile::printf(
                "WFSTDecoder::finish - bestFinalHyp.nLabelsNR > 0\n"
            );
        if ( bestFinalHyp->hist != NULL )
        {
            // walk the hyp back until the first DHHTYPE with score information
            DecHypHist* hist = bestFinalHyp->hist;
            while (hist->type != DHHTYPE)
            {
                hist = hist->prev;
                if (!hist)
                    error("WFSTDecoder::finish - failed to find DHHTYPE");
            }

            hist->score = bestFinalHyp->score;
            hist->lmScore = bestFinalHyp->lmScore;
            hist->acousticScore = bestFinalHyp->acousticScore;
        }
        return bestFinalHyp ;
    }
    else
    {
        return NULL ;
    }
}


// zl: getModel (above) can be called thousands of times but only a
// handful new model actually will be created. So move the new model
// creation to getNewModel to avoid unnecessary function call
WFSTModel *WFSTDecoder::getNewModel( WFSTTransition *trans )
{

    assert(activeModelsLookup[trans->id] == NULL);
    // If we did not find a match, grab a new WFSTModel element
    //   from the pool and add to lookup table and temp active list.
    WFSTModel *model = modelPool->getElem( trans ) ;
    model->next = newActiveModelsList ;
    newActiveModelsList = model ;
    if ( newActiveModelsListLastElem == NULL )
        newActiveModelsListLastElem = model ;

    activeModelsLookup[trans->id] = model ;
    nActiveModels++ ;
   return activeModelsLookup[trans->id] ;
}


void WFSTDecoder::joinNewActiveModelsList()
{
   if ( newActiveModelsList == NULL )
      return ;

   newActiveModelsListLastElem->next = activeModelsList ;
   activeModelsList = newActiveModelsList ;

   newActiveModelsList = newActiveModelsListLastElem = NULL ;
}


WFSTModel *WFSTDecoder::returnModel(
    WFSTModel *model , WFSTModel *prevActiveModel
)
{
#ifdef DEBUG
    if ( model == NULL )
        error("WFSTDecoder::returnModel - model is NULL") ;
    if ( activeModelsLookup[model->trans->id] != model )
        error("WFSTDecoder::returnModel - inconsistent entry in activeModelsLookup") ;
#endif

    // Reset the entry in the active models lookup array.
    activeModelsLookup[model->trans->id] = NULL ;

    // Return the model to the pool and remove it from the linked list of
    //   active models.
    if ( prevActiveModel == NULL )
    {
        // Model we are deactivating is at head of list.
        activeModelsList = model->next ;
        modelPool->returnElem( model ) ;
        --nActiveModels ;
        model = activeModelsList ;
    }
    else
    {
        // Model we are deactivating is not at head of list.
        prevActiveModel->next = model->next ;
        modelPool->returnElem( model ) ;
        --nActiveModels ;
        model = prevActiveModel->next ;
    }

   // Return the pointer to the next active model in the linked list.
   return model ;
}


void WFSTDecoder::reset()
{
   currFrame = -1 ;

   // Reset any active hypotheses
   resetActiveHyps() ;

   // Reset statistics
   nFrames = 0 ;
   totalActiveModels = 0 ;
   totalActiveEmitHyps = 0 ;
   totalActiveEndHyps = 0 ;
   totalProcEmitHyps = 0 ;
   totalProcEndHyps = 0 ;

   avgActiveModels = 0.0 ;
   avgActiveEmitHyps = 0.0 ;
   avgActiveEndHyps = 0.0 ;
   avgProcEmitHyps = 0.0 ;
   avgProcEndHyps = 0.0 ;

   // Reset pruning stuff
   bestEmitScore = LOG_ZERO ;
   currEmitPruneThresh = LOG_ZERO ;
#ifndef NO_BEST_END
   bestEndScore = LOG_ZERO ;
#endif
   currEndPruneThresh = LOG_ZERO ;
   bestHypHist = NULL ;
   currWordPruneThresh = LOG_ZERO ;

   normaliseScore = 0.0 ;
   if ( emitHypsHistogram != NULL )
      emitHypsHistogram->reset() ;

   if ( doLatticeGeneration )
   {
      // Reset lattice
      lattice->reset() ;
   }

   // Reset the bestFinalHyp
   if (bestFinalHyp)
       resetDecHyp( bestFinalHyp ) ;

}


void WFSTDecoder::resetActiveHyps()
{
   nActiveEmitHyps = 0 ;
   nActiveEndHyps = 0 ;
   nEmitHypsProcessed = 0 ;
   nEndHypsProcessed = 0 ;

   activeModelsList = NULL ;
   if ( activeModelsLookup != NULL )
   {
      for ( int i=0 ; i<activeModelsLookupLen ; i++ )
      {
         if ( activeModelsLookup[i] != NULL )
         {
            modelPool->returnElem( activeModelsLookup[i] ) ;
            --nActiveModels ;
            activeModelsLookup[i] = NULL ;
         }
      }

      if ( nActiveModels != 0 )
         error("WFSTDecoder::resetActiveHyps - nActiveModels has unexpected value") ;
   }

   if ( modelPool != NULL )  {
      if ( modelPool->numUsed() != 0 )
     error("WFSTDecoder::resetActiveHyps - modelPool had nUsed != 0 after reset") ;
   }

   newActiveModelsList = NULL ;
   newActiveModelsListLastElem = NULL ;
}


void WFSTDecoder::getBestHyp( int *numResultWords , int **resultWords , int **resultWordsTimes )
{
   error("WFSTDecoder::getBestHyp - not implemented") ;
}


/**
 * Make sure that our record of the number of active phone hyps is
 * accurate.
 */
void WFSTDecoder::checkActiveNumbers( bool checkModelPrevHyps )
{
    int cnt=0 , i ;
    WFSTModel *model ;

    for ( i=0 ; i<activeModelsLookupLen ; i++ )
    {
      model = activeModelsLookup[i] ;
      if ( model != NULL )
      {
         if ( model->nActiveHyps <= 0 )
            error("WFSTDecoder::checkActiveNumbers - active model has nActiveHyps <= 0") ;
         ++cnt ;
      }
   }

   if ( cnt != nActiveModels )
      error("WFSTDecoder::checkActiveNumbers - unexpected nActiveModels %d %d",cnt,nActiveModels);

   // compare result to one obtained by going through the activeModelsList
   cnt = 0 ;
   model = activeModelsList ;
   while ( model != NULL )
   {
      ++cnt ;
      model = model->next ;
   }

   if ( cnt != nActiveModels )
   {
      error("WFSTDecoder::checkActiveNumbers - unexpected active model count in list %d %d" ,
            cnt , nActiveModels ) ;
   }
}

void WFSTDecoder::resetDecHyp( DecHyp* hyp )
{
   hyp->score = LOG_ZERO;
   if (hyp->hist) {
       decHypHistPool->resetDecHypHist(hyp->hist);
       hyp->hist = NULL;
   }
}

void WFSTDecoder::registerLabel( DecHyp* hyp , int label )
{
   DecHypHistPool::registerLabel( hyp, label ) ;
}

// Changes Octavian 20060523
void WFSTDecoder::addLabelHist( DecHyp* hyp , int label )
{
   decHypHistPool->addLabelHistToDecHyp( hyp , label ) ;
}

int WFSTDecoder::addLatticeEntry( DecHyp *hyp , WFSTTransition *trans , int *fromState )
{
   if ( doLatticeGeneration == false )
      error("WFSTDecoder::addLatticeEntry - doLatticeGeneration == false") ;

   // The first element in the history linked list for endHyp should
   // be a lattice entry containing the lattice WFST state we want to
   // use as the from state.
   if ( (hyp->hist == NULL) || (hyp->hist->type != LATTICEDHHTYPE) )
      error("WFSTDecoder::addLatticeEntry - no lattice history found") ;

   LatticeDecHypHist *hist = (LatticeDecHypHist *)(hyp->hist) ;
   real newScore = hyp->acousticScore + hyp->lmScore - hist->accScore ;

   int toState ;
   toState = lattice->addEntry( hist->latState , trans->toState , trans->inLabel ,
                                trans->outLabel , newScore ) ;

   if ( fromState != NULL )
      *fromState = hist->latState ;

   // Remove the lattice history that we no longer need
   hyp->hist = hist->prev ;
   if ( --(hist->nConnect) == 0 )
   {
      decHypHistPool->returnSingleElem( (DecHypHist *)hist ) ;
      // the new hyp->hist->nConnect does not change
   }
   else if ( hyp->hist != NULL )
   {
      hyp->hist->nConnect++ ;
   }

   return toState ;
}

}
