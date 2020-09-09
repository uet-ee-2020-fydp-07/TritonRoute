/* Author: Matt Liberty */
/*
 * Copyright (c) 2020, The Regents of the University of California
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define BOOST_TEST_MODULE gc

#ifdef HAS_BOOST_UNIT_TEST_LIBRARY
// Shared library version
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
#else
// Header only version
#include <boost/test/included/unit_test.hpp>
#endif

#include "fixture.h"
#include "frDesign.h"
#include "gc/FlexGC.h"

using namespace fr;

// Fixture for GC tests
struct GCFixture : public Fixture
{
  GCFixture() : worker(design.get()) {}

  void testMarker(frMarker* marker,
                  frLayerNum layer_num,
                  frConstraintTypeEnum type,
                  const frBox& expected_bbox)
  {
    frBox bbox;
    marker->getBBox(bbox);

    BOOST_TEST(marker->getLayerNum() == layer_num);
    BOOST_TEST(marker->getConstraint());
    TEST_ENUM_EQUAL(marker->getConstraint()->typeId(), type);
    BOOST_TEST(bbox == expected_bbox);
  }

  void runGC()
  {
    // Needs to be run after all the objects are created but before gc
    initRegionQuery();

    // Run the GC engine
    const frBox work(0, 0, 2000, 2000);
    worker.setExtBox(work);
    worker.setDrcBox(work);

    worker.init();
    worker.main();
    worker.end();
  }

  FlexGCWorker worker;
};

BOOST_FIXTURE_TEST_SUITE(gc, GCFixture);

// Two touching metal shape from different nets generate a short
BOOST_AUTO_TEST_CASE(metal_short)
{
  // Setup
  frNet* n1 = makeNet("n1");
  frNet* n2 = makeNet("n2");

  makePathseg(n1, 2, {0, 0}, {500, 0});
  makePathseg(n2, 2, {500, 0}, {1000, 0});

  runGC();

  // Test the results
  auto& markers = worker.getMarkers();

  BOOST_TEST(markers.size() == 1);
  testMarker(markers[0].get(),
             2,
             frConstraintTypeEnum::frcShortConstraint,
             frBox(500, -50, 500, 50));
}

// Two touching metal shape from the same net must have sufficient
// overlap
BOOST_AUTO_TEST_CASE(metal_non_sufficient)
{
  // Setup
  frNet* n1 = makeNet("n1");

  makePathseg(n1, 2, {0, 0}, {0, 500});
  makePathseg(n1, 2, {0, 0}, {500, 0});

  runGC();

  // Test the results
  auto& markers = worker.getMarkers();

  BOOST_TEST(markers.size() == 1);
  testMarker(markers[0].get(),
             2,
             frConstraintTypeEnum::frcNonSufficientMetalConstraint,
             frBox(0, 0, 50, 50));
}

// Path seg less than min width flags a violation
BOOST_AUTO_TEST_CASE(min_width)
{
  // Setup
  frNet* n1 = makeNet("n1");

  makePathseg(n1, 2, {0, 0}, {500, 0}, 60);

  runGC();

  // Test the results
  auto& markers = worker.getMarkers();

  BOOST_TEST(markers.size() == 1);
  testMarker(markers[0].get(),
             2,
             frConstraintTypeEnum::frcMinWidthConstraint,
             frBox(0, -30, 500, 30));
}

// Abutting Path seg less than min width don't flag a violation
// as their combined width is ok
BOOST_AUTO_TEST_CASE(min_width_combines_shapes)
{
  // Setup
  frNet* n1 = makeNet("n1");

  makePathseg(n1, 2, {0, 0}, {500, 0}, 60);
  makePathseg(n1, 2, {0, 60}, {500, 60}, 60);

  runGC();

  // Test the results
  BOOST_TEST(worker.getMarkers().size() == 0);
}

// Check violation for off-grid points
BOOST_AUTO_TEST_CASE(off_grid)
{
  // Setup
  frNet* n1 = makeNet("n1");

  makePathseg(n1, 2, {1, 1}, {501, 1});

  runGC();

  // Test the results
  auto& markers = worker.getMarkers();

  BOOST_TEST(worker.getMarkers().size() == 1);
  testMarker(markers[0].get(),
             2,
             frConstraintTypeEnum::frcOffGridConstraint,
             frBox(1, -49, 501, 51));
}

// Check violation for corner spacing
BOOST_AUTO_TEST_CASE(corner_basic)
{
  // Setup
  makeCornerConstraint(2);

  frNet* n1 = makeNet("n1");

  makePathseg(n1, 2, {0, 0}, {500, 0});
  makePathseg(n1, 2, {500, 200}, {1000, 200});

  runGC();

  // Test the results
  auto& markers = worker.getMarkers();

  BOOST_TEST(worker.getMarkers().size() == 1);
  testMarker(markers[0].get(),
             2,
             frConstraintTypeEnum::frcLef58CornerSpacingConstraint,
             frBox(500, 50, 500, 150));
}

// Check no violation for corner spacing with EOL spacing
// (same as corner_basic but for eol)
BOOST_AUTO_TEST_CASE(corner_eol_no_violation)
{
  // Setup
  makeCornerConstraint(2, 200);

  frNet* n1 = makeNet("n1");

  makePathseg(n1, 2, {0, 0}, {500, 0});
  makePathseg(n1, 2, {500, 200}, {1000, 200});

  runGC();

  // Test the results
  auto& markers = worker.getMarkers();

  BOOST_TEST(worker.getMarkers().size() == 0);
}

// Check no violation for corner spacing with PRL > 0
// (same as corner_basic but for n2's pathseg begin pt)
BOOST_AUTO_TEST_CASE(corner_prl_no_violation)
{
  // Setup
  makeCornerConstraint(2);

  frNet* n1 = makeNet("n1");

  makePathseg(n1, 2, {0, 0}, {500, 0});
  makePathseg(n1, 2, {400, 200}, {1000, 200});

  runGC();

  // Test the results
  auto& markers = worker.getMarkers();

  BOOST_TEST(worker.getMarkers().size() == 0);
}

// Check violation for corner spacing on a concave corner
BOOST_AUTO_TEST_CASE(corner_concave, * boost::unit_test::disabled())
{
  // Setup
  makeCornerConstraint(2, /* no eol */ -1, frCornerTypeEnum::CONCAVE);

  frNet* n1 = makeNet("n1");

  makePathseg(n1, 2, {-50, 0}, {500, 0});
  makePathseg(n1, 2, {0, -50}, {0, 500});
  makePathseg(n1, 2, {200, 200}, {1000, 200});

  runGC();

  // Test the results
  auto& markers = worker.getMarkers();

  BOOST_TEST(worker.getMarkers().size() == 1);
  testMarker(markers[0].get(),
             2,
             frConstraintTypeEnum::frcLef58CornerSpacingConstraint,
             frBox(50, 50, 200, 200));
}

BOOST_AUTO_TEST_SUITE_END();
