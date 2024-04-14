/* Authors: Lutong Wang and Bangqi Xu */
/*
 * Copyright (c) 2019, The Regents of the University of California
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dr/FlexGridGraph.h"
#include "dr/FlexDR.h"
#include <chrono>

using namespace std;
using namespace fr;
using namespace chrono;

void FlexGridGraph::expand(FlexWavefrontGrid &currGrid, const frDirEnum &dir, 
                                      const FlexMazeIdx &dstMazeIdx1, const FlexMazeIdx &dstMazeIdx2,
                                      const frPoint &centerPt) {
  bool enableOutput = false;
  //bool enableOutput = true;
  frCost nextEstCost, nextPathCost;
  int gridX = currGrid.x();
  int gridY = currGrid.y();
  int gridZ = currGrid.z();

  getNextGrid(gridX, gridY, gridZ, dir);
  
  FlexMazeIdx nextIdx(gridX, gridY, gridZ);
  // get cost
  nextEstCost = getEstCost(nextIdx, dstMazeIdx1, dstMazeIdx2, dir);
  nextPathCost = getNextPathCost(currGrid, dir);  
  if (enableOutput) {
    std::cout << "  expanding from (" << currGrid.x() << ", " << currGrid.y() << ", " << currGrid.z() 
              << ") [pathCost / totalCost = " << currGrid.getPathCost() << " / " << currGrid.getCost() << "] to "
              << "(" << gridX << ", " << gridY << ", " << gridZ << ") [pathCost / totalCost = " 
              << nextPathCost << " / " << nextPathCost + nextEstCost << "]\n";
  }
  auto lNum = getLayerNum(currGrid.z());
  auto pathWidth = getDesign()->getTech()->getLayer(lNum)->getWidth();
  frPoint currPt;
  getPoint(currPt, gridX, gridY);
  frCoord currDist = abs(currPt.x() - centerPt.x()) + abs(currPt.y() - centerPt.y());

  // vlength calculation
  frCoord currVLengthX = 0;
  frCoord currVLengthY = 0;
  currGrid.getVLength(currVLengthX, currVLengthY);
  auto nextVLengthX = currVLengthX;
  auto nextVLengthY = currVLengthY;
  bool nextIsPrevViaUp = currGrid.isPrevViaUp();
  if (dir == frDirEnum::U || dir == frDirEnum::D) {
    nextVLengthX = 0;
    nextVLengthY = 0;
    nextIsPrevViaUp = (dir == frDirEnum::D); // up via if current path goes down
  } else {
    if (currVLengthX != std::numeric_limits<frCoord>::max() &&
        currVLengthY != std::numeric_limits<frCoord>::max()) {
      if (dir == frDirEnum::W || dir == frDirEnum::E) {
        nextVLengthX += getEdgeLength(currGrid.x(), currGrid.y(), currGrid.z(), dir);
      } else { 
        nextVLengthY += getEdgeLength(currGrid.x(), currGrid.y(), currGrid.z(), dir);
      }
    }
  }
  
  // tlength calculation
  auto currTLength = currGrid.getTLength();
  auto nextTLength = currTLength;
  // if there was a turn, then add tlength
  if (currTLength != std::numeric_limits<frCoord>::max()) {
    nextTLength += getEdgeLength(currGrid.x(), currGrid.y(), currGrid.z(), dir);
  }
  // if current is a turn, then reset tlength
  if (currGrid.getLastDir() != frDirEnum::UNKNOWN && currGrid.getLastDir() != dir) {
    nextTLength = getEdgeLength(currGrid.x(), currGrid.y(), currGrid.z(), dir);
  }
  // if current is a via, then reset tlength
  if (dir == frDirEnum::U || dir == frDirEnum::D) {
    nextTLength = std::numeric_limits<frCoord>::max();
  }

  FlexWavefrontGrid nextWavefrontGrid(gridX, gridY, gridZ, 
                                      currGrid.getLayerPathArea() + getEdgeLength(currGrid.x(), currGrid.y(), currGrid.z(), dir) * pathWidth, 
                                      nextVLengthX, nextVLengthY, nextIsPrevViaUp,
                                      nextTLength,
                                      currDist,
                                      nextPathCost, nextPathCost + nextEstCost, currGrid.getBackTraceBuffer());
  if (dir == frDirEnum::U || dir == frDirEnum::D) {
    nextWavefrontGrid.resetLayerPathArea();
    nextWavefrontGrid.resetLength();
    if (dir == frDirEnum::U) {
      nextWavefrontGrid.setPrevViaUp(false);
    } else {
      nextWavefrontGrid.setPrevViaUp(true);
    }
    nextWavefrontGrid.addLayerPathArea((dir == frDirEnum::U) ? getHalfViaEncArea(currGrid.z(), false) : getHalfViaEncArea(gridZ, true));
  }
  // update wavefront buffer
  auto tailDir = nextWavefrontGrid.shiftAddBuffer(dir);
  // non-buffer enablement is faster for ripup all
  // commit grid prev direction if needed
  auto tailIdx = getTailIdx(nextIdx, nextWavefrontGrid);
  if (tailDir != frDirEnum::UNKNOWN) {
    if (getPrevAstarNodeDir(tailIdx.x(), tailIdx.y(), tailIdx.z()) == frDirEnum::UNKNOWN ||
        getPrevAstarNodeDir(tailIdx.x(), tailIdx.y(), tailIdx.z()) == tailDir) {
      setPrevAstarNodeDir(tailIdx.x(), tailIdx.y(), tailIdx.z(), tailDir);
      wavefront.push(nextWavefrontGrid);
      if (enableOutput) {
        std::cout << "    commit (" << tailIdx.x() << ", " << tailIdx.y() << ", " << tailIdx.z() << ") prev accessing dir = " << (int)tailDir << "\n";
      }
    }
  } else {  
    // add to wavefront
    wavefront.push(nextWavefrontGrid);
  }

  return;
}

void FlexGridGraph::comp_exp_funcs_simd(const FlexWavefrontGrid &currGrid )  {

  auto t1 = high_resolution_clock::now();
	
	frMIdx gridX, gridY, gridZ;
  bool getZDir_zIdx1, getZDir_zIdx2, getZDir_zIdx3;
 
  gridX = currGrid.x();
  gridY = currGrid.y();
  gridZ = currGrid.z();
  
	getZDir_zIdx1 = getZDir(gridZ);
	getZDir_zIdx2 = getZDir(gridZ - 1);
	getZDir_zIdx3 = getZDir(gridZ + 1);
	
	Vec = _mm256_setr_epi32 (gridX, gridY, gridZ, getZDir_zIdx1, getZDir_zIdx2,  getZDir_zIdx3, 0, 0);
	
	X = _mm256_add_epi32(_mm256_permutevar8x32_epi32(Vec, X_id), X_inc);
  Y = _mm256_add_epi32(_mm256_permutevar8x32_epi32(Vec, Y_id), Y_inc);
  Z = _mm256_add_epi32(_mm256_permutevar8x32_epi32(Vec, Z_id), Z_inc);
  idx_mask = _mm256_cmpgt_epi32_mask(
    _mm256_permutevar8x32_epi32(Vec, idx_mask_id), Zeros_cmp);

	
	// isValid_mask = (X | Y | Z) >= 0  &  (X < XMax) & (Y < YMax) & (Z < ZMax);
	
	isValid_mask = _kand_mask8(
		_mm256_cmp_epi32_mask(_mm256_or_epi32(_mm256_or_epi32(X, Y), Z), Zeros_cmp, 5), 
		_kand_mask8( _mm256_cmp_epi32_mask(X, XMax, 1),
		_kand_mask8( _mm256_cmp_epi32_mask(Y, YMax, 1) ,_mm256_cmp_epi32_mask(Z, ZMax, 1))));

	// Idx = (idx_mask) ?  X + Y*XMax + Z*XMax*YMax :  Y + X*YMax + Z*XMax*YMax
	Idx = _mm256_mask_blend_epi32(idx_mask,
					_mm256_add_epi32(_mm256_mul_epi32(_mm256_add_epi32(_mm256_mul_epi32(Z, XMax), X), YMax), Y),
					_mm256_add_epi32(_mm256_mul_epi32(_mm256_add_epi32(_mm256_mul_epi32(Z, YMax), Y), XMax), X));

	// HasEdge Is Valid will be used for loading numbers from bits array
	
	// bits_vals = (isValid_mask) ? bits[Idx] : 0
	bit_vals = _mm512_mask_i32gather_epi64 (Zeros64, isValid_mask, Idx, (const long long*)&bits[0], 8);
	
	bit_vals_he_hgc_ib = _mm512_permutexvar_epi64(he_hgc_ib_bits_id, bit_vals);
	
	has_edge = _mm512_and_epi64(_mm512_srlv_epi64(bit_vals_he_hgc_ib, he_id), Ones_And);
									
	has_grid_cost = _mm512_and_epi64( _mm512_srlv_epi64(bit_vals_he_hgc_ib, hgc_id) , Ones_And);
											
	is_blocked = _mm512_and_epi64(_mm512_srlv_epi64(bit_vals_he_hgc_ib, ib_id), Ones_And);	
	
	bit_vals_hdc_hmc_hsc = _mm512_permutexvar_epi64(hdc_hmc_hsc_id, bit_vals);								
	
	has_drc_cost = _mm512_srlv_epi64(_mm512_and_epi64(bit_vals_hdc_hmc_hsc, bits_tmp_hdc), bits_pos_hdc);
									
	has_marker_cost = _mm512_srlv_epi64(_mm512_and_epi64(bit_vals_hdc_hmc_hsc, bits_tmp_hmc), bits_pos_hmc);
									
	has_shape_cost = _mm512_andnot_epi64(
    _mm512_permutex_epi64( _mm512_mask_blend_epi64(0b00000011, Zeros64, has_edge), 0b00001011),
    _mm512_srlv_epi64(_mm512_and_epi64(bit_vals_hdc_hmc_hsc , bits_tmp_hsc), bits_pos_hsc));


  auto t2 = high_resolution_clock::now();

  bool HE_N = hasEdge(gridX, gridY, gridZ, frDirEnum::N);
  bool HE_S = hasEdge(gridX, gridY, gridZ, frDirEnum::S);
  bool HE_U = hasEdge(gridX, gridY, gridZ, frDirEnum::U);
  bool HE_D = hasEdge(gridX, gridY, gridZ, frDirEnum::D);
  bool HE_E = hasEdge(gridX, gridY, gridZ, frDirEnum::E);
  bool HE_W = hasEdge(gridX, gridY, gridZ, frDirEnum::W);

  bool HGC_N = hasGridCost(gridX, gridY, gridZ, frDirEnum::N);
  bool HGC_S = hasGridCost(gridX, gridY, gridZ, frDirEnum::S);
  bool HGC_U = hasGridCost(gridX, gridY, gridZ, frDirEnum::U);
  bool HGC_D = hasGridCost(gridX, gridY, gridZ, frDirEnum::D);
  bool HGC_E = hasGridCost(gridX, gridY, gridZ, frDirEnum::E);
  bool HGC_W = hasGridCost(gridX, gridY, gridZ, frDirEnum::W);

  bool IB_N = isBlocked(gridX, gridY, gridZ, frDirEnum::N);
  bool IB_S = isBlocked(gridX, gridY, gridZ, frDirEnum::S);
  bool IB_U = isBlocked(gridX, gridY, gridZ, frDirEnum::U);
  bool IB_D = isBlocked(gridX, gridY, gridZ, frDirEnum::D);
  bool IB_E = isBlocked(gridX, gridY, gridZ, frDirEnum::E);
  bool IB_W = isBlocked(gridX, gridY, gridZ, frDirEnum::W);

  bool HM_N = hasMarkerCost(gridX, gridY, gridZ, frDirEnum::N);
  bool HM_S = hasMarkerCost(gridX, gridY, gridZ, frDirEnum::S);
  bool HM_U = hasMarkerCost(gridX, gridY, gridZ, frDirEnum::U);
  bool HM_D = hasMarkerCost(gridX, gridY, gridZ, frDirEnum::D);
  bool HM_E = hasMarkerCost(gridX, gridY, gridZ, frDirEnum::E);
  bool HM_W = hasMarkerCost(gridX, gridY, gridZ, frDirEnum::W);

  bool HDC_N = hasDRCCost(gridX, gridY, gridZ, frDirEnum::N);
  bool HDC_S = hasDRCCost(gridX, gridY, gridZ, frDirEnum::S);
  bool HDC_U = hasDRCCost(gridX, gridY, gridZ, frDirEnum::U);
  bool HDC_D = hasDRCCost(gridX, gridY, gridZ, frDirEnum::D);
  bool HDC_E = hasDRCCost(gridX, gridY, gridZ, frDirEnum::E);
  bool HDC_W = hasDRCCost(gridX, gridY, gridZ, frDirEnum::W);

  bool HSC_N = hasShapeCost(gridX, gridY, gridZ, frDirEnum::N);
  bool HSC_S = hasShapeCost(gridX, gridY, gridZ, frDirEnum::S);
  bool HSC_U = hasShapeCost(gridX, gridY, gridZ, frDirEnum::U);
  bool HSC_D = hasShapeCost(gridX, gridY, gridZ, frDirEnum::D);
  bool HSC_E = hasShapeCost(gridX, gridY, gridZ, frDirEnum::E);
  bool HSC_W = hasShapeCost(gridX, gridY, gridZ, frDirEnum::W);

  auto t3 = high_resolution_clock::now();

  auto duration1 = duration_cast<nanoseconds>(t2 - t1);
	auto duration2 = duration_cast<nanoseconds>(t3 - t2);

  cout << "Time Taken By Compiler Intrinsics is " << duration1.count() << " nanoseconds" << endl;
  cout << "Time Taken By 6 Functions for 6 directions is " << duration2.count() << " nanoseconds" << endl;
  cout << endl;


  
}


void FlexGridGraph::expandWavefront(FlexWavefrontGrid &currGrid, const FlexMazeIdx &dstMazeIdx1, 
                                               const FlexMazeIdx &dstMazeIdx2, const frPoint &centerPt) {
  bool enableOutput = false;
  //bool enableOutput = true;
  if (enableOutput) {
    cout << "start expand from (" << currGrid.x() << ", " << currGrid.y() << ", " << currGrid.z() << ")\n";
  }
  //if (currGrid.y() == 19 && currGrid.z() == 0) {
  //  cout <<"is expandable (" <<currGrid.x() <<", " <<currGrid.y() <<", " <<currGrid.z() <<") NESWUD "
  //       <<isExpandable(currGrid, frDirEnum::N)
  //       <<isExpandable(currGrid, frDirEnum::E)
  //       <<isExpandable(currGrid, frDirEnum::S)
  //       <<isExpandable(currGrid, frDirEnum::W)
  //       <<isExpandable(currGrid, frDirEnum::U)
  //       <<isExpandable(currGrid, frDirEnum::D)
  //       <<endl;
  //  cout <<"has edge " 
  //       <<gridGraph.hasEdge(currGrid.x(), currGrid.y(), currGrid.z(), frDirEnum::N)
  //       <<gridGraph.hasEdge(currGrid.x(), currGrid.y(), currGrid.z(), frDirEnum::E)
  //       <<gridGraph.hasEdge(currGrid.x(), currGrid.y(), currGrid.z(), frDirEnum::S)
  //       <<gridGraph.hasEdge(currGrid.x(), currGrid.y(), currGrid.z(), frDirEnum::W)
  //       <<gridGraph.hasEdge(currGrid.x(), currGrid.y(), currGrid.z(), frDirEnum::U)
  //       <<gridGraph.hasEdge(currGrid.x(), currGrid.y(), currGrid.z(), frDirEnum::D)
  //       <<endl;
  //  int gridX = currGrid.x();
  //  int gridY = currGrid.y();
  //  int gridZ = currGrid.z();
  //  if (!gridGraph.hasEdge(gridX, gridY, gridZ, frDirEnum::E)) {
  //    ;
  //  } else {
  //    getNextGrid(gridX, gridY, gridZ, frDirEnum::E);
  //    if (gridGraph.isBlocked(gridX, gridY, gridZ)) {
  //      cout <<"blocked" <<endl;
  //    } else if (gridGraph.isSrc(gridX, gridY, gridZ)) {
  //      cout <<"src" <<endl;
  //    } else if (gridGraph.getPrevAstarNodeDir(gridX, gridY, gridZ) != frDirEnum::UNKNOWN) {
  //      cout <<"visited" <<endl;
  //    } else {
  //      ;
  //    }
  //  }
  //}
  //auto tmpGrid = currWavefrontGrid;
  //// commit grid prev direction if needed
  //auto tailIdx = getTailIdx(nextIdx, nextWavefrontGrid);
  //if (tailDir != frDirEnum::UNKNOWN) {
  //  if (getPrevAstarNodeDir(tailIdx.x(), tailIdx.y(), tailIdx.z()) == frDirEnum::UNKNOWN) {
  //    ;
  //  }
  //}

  comp_exp_funcs_simd(currGrid);

  // N
  if (isExpandable(currGrid, frDirEnum::N)) {
    expand(currGrid, frDirEnum::N, dstMazeIdx1, dstMazeIdx2, centerPt);
  }
  // else {
  //   std::cout <<"no N" <<endl;
  // }
  // E
  if (isExpandable(currGrid, frDirEnum::E)) {
    expand(currGrid, frDirEnum::E, dstMazeIdx1, dstMazeIdx2, centerPt);
  }
  // else {
  //   std::cout <<"no E" <<endl;
  // }
  // S
  if (isExpandable(currGrid, frDirEnum::S)) {
    expand(currGrid, frDirEnum::S, dstMazeIdx1, dstMazeIdx2, centerPt);
  }
  // else {
  //   std::cout <<"no S" <<endl;
  // }
  // W
  if (isExpandable(currGrid, frDirEnum::W)) {
    expand(currGrid, frDirEnum::W, dstMazeIdx1, dstMazeIdx2, centerPt);
  }
  // else {
  //   std::cout <<"no W" <<endl;
  // }
  // U
  if (isExpandable(currGrid, frDirEnum::U)) {
    expand(currGrid, frDirEnum::U, dstMazeIdx1, dstMazeIdx2, centerPt);
  }
  // else {
  //   std::cout <<"no U" <<endl;
  // }
  // D
  if (isExpandable(currGrid, frDirEnum::D)) {
    expand(currGrid, frDirEnum::D, dstMazeIdx1, dstMazeIdx2, centerPt);
  }
  // else {
  //   std::cout <<"no D" <<endl;
  // }
}

frCost FlexGridGraph::getEstCost(const FlexMazeIdx &src, const FlexMazeIdx &dstMazeIdx1,
                                const FlexMazeIdx &dstMazeIdx2, const frDirEnum &dir) const {
  //bool enableOutput = true;
  bool enableOutput = false;
  if (enableOutput) {
    cout <<"est from (" <<src.x() <<", " <<src.y() <<", " <<src.z() <<") "
         <<"to ("       <<dstMazeIdx1.x() <<", " <<dstMazeIdx1.y() <<", " <<dstMazeIdx1.z() <<") ("
                        <<dstMazeIdx2.x() <<", " <<dstMazeIdx2.y() <<", " <<dstMazeIdx2.z() <<")";
  }
  // bend cost
  int bendCnt = 0;
  int forbiddenPenalty = 0;
  frPoint srcPoint, dstPoint1, dstPoint2;
  getPoint(srcPoint, src.x(), src.y());
  getPoint(dstPoint1, dstMazeIdx1.x(), dstMazeIdx1.y());
  getPoint(dstPoint2, dstMazeIdx2.x(), dstMazeIdx2.y());
  frCoord minCostX = max(max(dstPoint1.x() - srcPoint.x(), srcPoint.x() - dstPoint2.x()), 0) * 1;
  frCoord minCostY = max(max(dstPoint1.y() - srcPoint.y(), srcPoint.y() - dstPoint2.y()), 0) * 1;
  frCoord minCostZ = max(max(getZHeight(dstMazeIdx1.z()) - getZHeight(src.z()), 
                       getZHeight(src.z()) - getZHeight(dstMazeIdx2.z())), 0) * 1;
  if (enableOutput) {
    cout <<" x/y/z min cost = (" <<minCostX <<", " <<minCostY <<", " <<minCostZ <<") " <<endl;
  }

  bendCnt += (minCostX && dir != frDirEnum::UNKNOWN && dir != frDirEnum::E && dir != frDirEnum::W) ? 1 : 0;
  bendCnt += (minCostY && dir != frDirEnum::UNKNOWN && dir != frDirEnum::S && dir != frDirEnum::N) ? 1 : 0;
  bendCnt += (minCostZ && dir != frDirEnum::UNKNOWN && dir != frDirEnum::U && dir != frDirEnum::D) ? 1 : 0;
  if (enableOutput) {
    cout << "  est cost = " << minCostX + minCostY + minCostZ + bendCnt << endl;
  }

  int gridX = src.x();
  int gridY = src.y();
  int gridZ = src.z();
  getNextGrid(gridX, gridY, gridZ, dir);
  frPoint nextPoint;
  getPoint(nextPoint, gridX, gridY);
  // avoid propagating to location that will cause fobidden via spacing to boundary pin
  if (DBPROCESSNODE == "GF14_13M_3Mx_2Cx_4Kx_2Hx_2Gx_LB") {
    if (drWorker && drWorker->getDRIter() >= 30 && drWorker->getRipupMode() == 0) {
      if (dstMazeIdx1 == dstMazeIdx2 && gridZ == dstMazeIdx1.z()) {
        auto layerNum = (gridZ + 1) * 2;
        auto layer = getDesign()->getTech()->getLayer(layerNum);
        bool isH = (layer->getDir() == frPrefRoutingDirEnum::frcHorzPrefRoutingDir);
        if (isH) {
          auto gap = abs(nextPoint.y() - dstPoint1.y());
          if (gap &&
              (getDesign()->getTech()->isVia2ViaForbiddenLen(gridZ, false, false, false, gap, false) || layerNum - 2 < BOTTOM_ROUTING_LAYER) &&
              (getDesign()->getTech()->isVia2ViaForbiddenLen(gridZ, true, true, false, gap, false) || layerNum + 2 > getDesign()->getTech()->getTopLayerNum())) {
            forbiddenPenalty = layer->getPitch() * ggDRCCost * 20;
          }
        } else {
          auto gap = abs(nextPoint.x() - dstPoint1.x());
          if (gap &&
              (getDesign()->getTech()->isVia2ViaForbiddenLen(gridZ, false, false, true, gap, false) || layerNum - 2 < BOTTOM_ROUTING_LAYER) &&
              (getDesign()->getTech()->isVia2ViaForbiddenLen(gridZ, true, true, true, gap, false) || layerNum + 2 > getDesign()->getTech()->getTopLayerNum())) {
            forbiddenPenalty = layer->getPitch() * ggDRCCost * 20;
          }
        }
      }
    }
  }

  return (minCostX + minCostY + minCostZ + bendCnt + forbiddenPenalty);

}

frDirEnum FlexGridGraph::getLastDir(const std::bitset<WAVEFRONTBITSIZE> &buffer) const {
  auto currDirVal = buffer.to_ulong() & 0b111u;
  return static_cast<frDirEnum>(currDirVal);
}

void FlexGridGraph::getNextGrid(frMIdx &gridX, frMIdx &gridY, frMIdx &gridZ, const frDirEnum dir) const {
  switch(dir) {
    case frDirEnum::E:
      ++gridX;
      break;
    case frDirEnum::S:
      --gridY;
      break;
    case frDirEnum::W:
      --gridX;
      break;
    case frDirEnum::N:
      ++gridY;
      break;
    case frDirEnum::U:
      ++gridZ;
      break;
    case frDirEnum::D:
      --gridZ;
      break;
    default:
      ;
  }
  return;
}

void FlexGridGraph::getPrevGrid(frMIdx &gridX, frMIdx &gridY, frMIdx &gridZ, const frDirEnum dir) const {
  switch(dir) {
    case frDirEnum::E:
      --gridX;
      break;
    case frDirEnum::S:
      ++gridY;
      break;
    case frDirEnum::W:
      ++gridX;
      break;
    case frDirEnum::N:
      --gridY;
      break;
    case frDirEnum::U:
      --gridZ;
      break;
    case frDirEnum::D:
      ++gridZ;
      break;
    default:
      ;
  }
  return;
}

/*inline*/ frCost FlexGridGraph::getNextPathCost(const FlexWavefrontGrid &currGrid, const frDirEnum &dir) const {
  // bool enableOutput = true;
  bool enableOutput = false;
  frMIdx gridX = currGrid.x();
  frMIdx gridY = currGrid.y();
  frMIdx gridZ = currGrid.z();
  frCost nextPathCost = currGrid.getPathCost();
  // bending cost
  auto currDir = currGrid.getLastDir();
  auto lNum = getLayerNum(currGrid.z());
  auto pathWidth = getDesign()->getTech()->getLayer(lNum)->getWidth();

  if (currDir != dir && currDir != frDirEnum::UNKNOWN) {
    // original
    ++nextPathCost;
  }

  // via2viaForbiddenLen enablement
  if (dir == frDirEnum::U || dir == frDirEnum::D) {
    frCoord currVLengthX = 0;
    frCoord currVLengthY = 0;
    currGrid.getVLength(currVLengthX, currVLengthY);
    bool isCurrViaUp = (dir == frDirEnum::U);
    bool isForbiddenVia2Via = false;
    // check only y
    if (currVLengthX == 0 && currVLengthY > 0 && getTech()->isVia2ViaForbiddenLen(gridZ, !(currGrid.isPrevViaUp()), !isCurrViaUp, false, currVLengthY, false)) {
      isForbiddenVia2Via = true;
    // check only x
    } else if (currVLengthX > 0 && currVLengthY == 0 && getTech()->isVia2ViaForbiddenLen(gridZ, !(currGrid.isPrevViaUp()), !isCurrViaUp, true, currVLengthX, false)) {
      isForbiddenVia2Via = true;
    // check both x and y
    } else if (currVLengthX > 0 && currVLengthY > 0 && 
               (getTech()->isVia2ViaForbiddenLen(gridZ, !(currGrid.isPrevViaUp()), !isCurrViaUp, false, currVLengthY) &&
                getTech()->isVia2ViaForbiddenLen(gridZ, !(currGrid.isPrevViaUp()), !isCurrViaUp, true, currVLengthX))) {
      isForbiddenVia2Via = true;
    }

    if (isForbiddenVia2Via) {
      if (drWorker && drWorker->getDRIter() >= 3) {
        nextPathCost += ggMarkerCost * getEdgeLength(gridX, gridY, gridZ, dir);
      } else {
        nextPathCost += ggDRCCost * getEdgeLength(gridX, gridY, gridZ, dir);
      }
    }
  }

  // via2turn forbidden len enablement
  frCoord tLength    = std::numeric_limits<frCoord>::max();
  frCoord tLengthDummy = 0;
  bool    isTLengthViaUp = false;
  bool    isForbiddenTLen = false;
  if (currDir != frDirEnum::UNKNOWN && currDir != dir) {
    // next dir is a via
    if (dir == frDirEnum::U || dir == frDirEnum::D) {
      isTLengthViaUp = (dir == frDirEnum::U);
      // if there was a turn before
      if (tLength != std::numeric_limits<frCoord>::max()) {
        if (currDir == frDirEnum::W || currDir == frDirEnum::E) {
          tLength = currGrid.getTLength();
          if (getTech()->isViaForbiddenTurnLen(gridZ, !isTLengthViaUp, true, tLength)) {
            isForbiddenTLen = true;
          }
        } else if (currDir == frDirEnum::S || currDir == frDirEnum::N) {
          tLength = currGrid.getTLength();
          if (getTech()->isViaForbiddenTurnLen(gridZ, !isTLengthViaUp, false, tLength)) {
            isForbiddenTLen = true;
          }
        }
      }
    // curr is a planar turn
    } else {
      isTLengthViaUp = currGrid.isPrevViaUp();
      if (currDir == frDirEnum::W || currDir == frDirEnum::E) {
        currGrid.getVLength(tLength, tLengthDummy);
        if (getTech()->isViaForbiddenTurnLen(gridZ, !isTLengthViaUp, true, tLength)) {
          isForbiddenTLen = true;
        }
      } else if (currDir == frDirEnum::S || currDir == frDirEnum::N) {
        currGrid.getVLength(tLengthDummy, tLength);
        if (getTech()->isViaForbiddenTurnLen(gridZ, !isTLengthViaUp, false, tLength)) {
          isForbiddenTLen = true;
        }
      }
    }
    if (isForbiddenTLen) {
      if (drWorker && drWorker->getDRIter() >= 3) {
        nextPathCost += ggDRCCost * getEdgeLength(gridX, gridY, gridZ, dir);
      } else {
        nextPathCost += ggMarkerCost * getEdgeLength(gridX, gridY, gridZ, dir);
      }
    }
  }

  bool gridCost   = hasGridCost(gridX, gridY, gridZ, dir);
  bool drcCost    = hasDRCCost(gridX, gridY, gridZ, dir);
  bool markerCost = hasMarkerCost(gridX, gridY, gridZ, dir);
  bool shapeCost  = hasShapeCost(gridX, gridY, gridZ, dir);
  bool blockCost  = isBlocked(gridX, gridY, gridZ, dir);
  bool guideCost  = hasGuide(gridX, gridY, gridZ, dir);

  // temporarily disable guideCost
  nextPathCost += getEdgeLength(gridX, gridY, gridZ, dir)
                  + (gridCost   ? GRIDCOST         * getEdgeLength(gridX, gridY, gridZ, dir) : 0)
                  + (drcCost    ? ggDRCCost        * getEdgeLength(gridX, gridY, gridZ, dir) : 0)
                  + (markerCost ? ggMarkerCost     * getEdgeLength(gridX, gridY, gridZ, dir) : 0)
                  + (shapeCost  ? SHAPECOST        * getEdgeLength(gridX, gridY, gridZ, dir) : 0)
                  + (blockCost  ? BLOCKCOST        * pathWidth * 20                          : 0)
                  + (!guideCost ? GUIDECOST        * getEdgeLength(gridX, gridY, gridZ, dir) : 0);
  if (enableOutput) {
    cout <<"edge grid/shape/drc/marker/blk/length = " 
         <<hasGridCost(gridX, gridY, gridZ, dir)   <<"/"
         <<hasShapeCost(gridX, gridY, gridZ, dir)  <<"/"
         <<hasDRCCost(gridX, gridY, gridZ, dir)    <<"/"
         <<hasMarkerCost(gridX, gridY, gridZ, dir) <<"/"
         <<isBlocked(gridX, gridY, gridZ, dir) <<"/"
         <<getEdgeLength(gridX, gridY, gridZ, dir) <<endl;
  }
  return nextPathCost;

}

/*inline*/ FlexMazeIdx FlexGridGraph::getTailIdx(const FlexMazeIdx &currIdx, const FlexWavefrontGrid &currGrid) const {
  int gridX = currIdx.x();
  int gridY = currIdx.y();
  int gridZ = currIdx.z();
  auto backTraceBuffer = currGrid.getBackTraceBuffer();
  for (int i = 0; i < WAVEFRONTBUFFERSIZE; ++i) {
    int currDirVal = backTraceBuffer.to_ulong() - ((backTraceBuffer.to_ulong() >> DIRBITSIZE) << DIRBITSIZE);
    frDirEnum currDir = static_cast<frDirEnum>(currDirVal);
    backTraceBuffer >>= DIRBITSIZE;
    getPrevGrid(gridX, gridY, gridZ, currDir);
  }
  return FlexMazeIdx(gridX, gridY, gridZ);
}

/*inline*/ bool FlexGridGraph::isExpandable(const FlexWavefrontGrid &currGrid, frDirEnum dir) const {
  //bool enableOutput = true;
  bool enableOutput = false;
  frMIdx gridX = currGrid.x();
  frMIdx gridY = currGrid.y();
  frMIdx gridZ = currGrid.z();
  bool hg = hasEdge(gridX, gridY, gridZ, dir);
  if (enableOutput) {
    if (!hasEdge(gridX, gridY, gridZ, dir)) {
      cout <<"no edge@(" <<gridX <<", " <<gridY <<", " <<gridZ <<") " <<(int)dir <<endl;
    }
    if (hasEdge(gridX, gridY, gridZ, dir) && !hasGuide(gridX, gridY, gridZ, dir)) {
      cout <<"no guide@(" <<gridX <<", " <<gridY <<", " <<gridZ <<") " <<(int)dir <<endl;
    }
  }
  reverse(gridX, gridY, gridZ, dir);
  if (!hg || 
      isSrc(gridX, gridY, gridZ) || 
      (getPrevAstarNodeDir(gridX, gridY, gridZ) != frDirEnum::UNKNOWN) || // comment out for non-buffer enablement
      currGrid.getLastDir() == dir) {
    return false;
  } else {
    return true;
  }
}

void FlexGridGraph::traceBackPath(const FlexWavefrontGrid &currGrid, vector<FlexMazeIdx> &path, vector<FlexMazeIdx> &root,
                                  FlexMazeIdx &ccMazeIdx1, FlexMazeIdx &ccMazeIdx2) const {
  //bool enableOutput = true;
  bool enableOutput = false;
  if (enableOutput) {
    cout << "    start traceBackPath...\n";
  }
  frDirEnum prevDir = frDirEnum::UNKNOWN, currDir = frDirEnum::UNKNOWN;
  int currX = currGrid.x(), currY = currGrid.y(), currZ = currGrid.z();
  // pop content in buffer
  auto backTraceBuffer = currGrid.getBackTraceBuffer();
  for (int i = 0; i < WAVEFRONTBUFFERSIZE; ++i) {
    // current grid is src
    if (isSrc(currX, currY, currZ)) {
      break;
    }
    // get last direction
    currDir = getLastDir(backTraceBuffer);
    backTraceBuffer >>= DIRBITSIZE;
    if (currDir == frDirEnum::UNKNOWN) {
      cout << "Warning: unexpected direction in tracBackPath\n";
      break;
    }
    root.push_back(FlexMazeIdx(currX, currY, currZ));
    // push point to path
    if (currDir != prevDir) {
      path.push_back(FlexMazeIdx(currX, currY, currZ));
      if (enableOutput) {
        cout <<" -- (" <<currX <<", " <<currY <<", " <<currZ <<")";
      }
    }
    getPrevGrid(currX, currY, currZ, currDir);
    prevDir = currDir;
  }
  // trace back according to grid prev dir
  while (isSrc(currX, currY, currZ) == false) {
    // get last direction
    currDir = getPrevAstarNodeDir(currX, currY, currZ);
    root.push_back(FlexMazeIdx(currX, currY, currZ));
    if (currDir == frDirEnum::UNKNOWN) {
      cout << "Warning: unexpected direction in tracBackPath\n";
      break;
    }
    if (currDir != prevDir) {
      path.push_back(FlexMazeIdx(currX, currY, currZ));
      if (enableOutput) {
        cout <<" -- (" <<currX <<", " <<currY <<", " <<currZ <<")";
      }
    }
    getPrevGrid(currX, currY, currZ, currDir);
    prevDir = currDir;
  }
  // add final path to src, only add when path exists; no path exists (src = dst)
  if (!path.empty()) {
    path.push_back(FlexMazeIdx(currX, currY, currZ));
    if (enableOutput) {
      cout <<" -- (" <<currX <<", " <<currY <<", " <<currZ <<")";
    }
  }
  for (auto &mi: path) {
    ccMazeIdx1.set(min(ccMazeIdx1.x(), mi.x()),
                   min(ccMazeIdx1.y(), mi.y()),
                   min(ccMazeIdx1.z(), mi.z()));
    ccMazeIdx2.set(max(ccMazeIdx2.x(), mi.x()),
                   max(ccMazeIdx2.y(), mi.y()),
                   max(ccMazeIdx2.z(), mi.z()));
  }
  if (enableOutput) {
    cout <<endl;
  }

}

bool FlexGridGraph::search(vector<FlexMazeIdx> &connComps, drPin* nextPin, vector<FlexMazeIdx> &path, 
                           FlexMazeIdx &ccMazeIdx1, FlexMazeIdx &ccMazeIdx2, const frPoint &centerPt) {
  //bool enableOutput = true;
  bool enableOutput = false;
  int stepCnt = 0;

  // prep nextPinBox
  frMIdx xDim, yDim, zDim;
  getDim(xDim, yDim, zDim);
  FlexMazeIdx dstMazeIdx1(xDim - 1, yDim - 1, zDim - 1);
  FlexMazeIdx dstMazeIdx2(0, 0, 0);
  FlexMazeIdx mi;
  for (auto &ap: nextPin->getAccessPatterns()) {
    ap->getMazeIdx(mi);
    dstMazeIdx1.set(min(dstMazeIdx1.x(), mi.x()),
                    min(dstMazeIdx1.y(), mi.y()),
                    min(dstMazeIdx1.z(), mi.z()));
    dstMazeIdx2.set(max(dstMazeIdx2.x(), mi.x()),
                    max(dstMazeIdx2.y(), mi.y()),
                    max(dstMazeIdx2.z(), mi.z()));
  }

  wavefront.cleanup();
  // init wavefront
  frPoint currPt;
  for (auto &idx: connComps) {
    if (isDst(idx.x(), idx.y(), idx.z())) {
      if (enableOutput) {
        cout <<"message: astarSearch dst covered (" <<idx.x() <<", " <<idx.y() <<", " <<idx.z() <<")" <<endl;
      }
      path.push_back(FlexMazeIdx(idx.x(), idx.y(), idx.z()));
      return true;
    }
    // get min area min length
    auto lNum = getLayerNum(idx.z());
    auto minAreaConstraint = getDesign()->getTech()->getLayer(lNum)->getAreaConstraint();
    frCoord fakeArea = minAreaConstraint ? minAreaConstraint->getMinArea() : 0;
    getPoint(currPt, idx.x(), idx.y());
    frCoord currDist = abs(currPt.x() - centerPt.x()) + abs(currPt.y() - centerPt.y());
    FlexWavefrontGrid currGrid(idx.x(), idx.y(), idx.z(), fakeArea, 
                               std::numeric_limits<frCoord>::max(), std::numeric_limits<frCoord>::max(), true, 
                               std::numeric_limits<frCoord>::max(),
                               currDist, 0, getEstCost(idx, dstMazeIdx1, dstMazeIdx2, frDirEnum::UNKNOWN));
    wavefront.push(currGrid);
    if (enableOutput) {
      cout <<"src add to wavefront (" <<idx.x() <<", " <<idx.y() <<", " <<idx.z() <<")" <<endl;
    }
  }
  while(!wavefront.empty()) {
    auto currGrid = wavefront.top();
    wavefront.pop();
    if (getPrevAstarNodeDir(currGrid.x(), currGrid.y(), currGrid.z()) != frDirEnum::UNKNOWN) {
      continue;
    }

    // test
    if (enableOutput) {
      ++stepCnt;
    }
    // if (stepCnt % 100000 == 0) {
    //   std::cout << "wavefront size = " << wavefront.size() << " at step = " << stepCnt << "\n";
    // }
    if (isDst(currGrid.x(), currGrid.y(), currGrid.z())) {
      traceBackPath(currGrid, path, connComps, ccMazeIdx1, ccMazeIdx2);
      if (enableOutput) {
        cout << "path found. stepCnt = " << stepCnt << "\n";
      }
      return true;
    } else {
      // expand and update wavefront
      expandWavefront(currGrid, dstMazeIdx1, dstMazeIdx2, centerPt);
    }
    
  }
  return false;
}

