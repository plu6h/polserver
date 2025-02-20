/** @file
 *
 * @par History
 */

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#include "../../clib/logfacility.h"
#include "../../clib/rawtypes.h"
#include "../globals/uvars.h"
#include "../item/item.h"
#include "../item/itemdesc.h"
#include "../multi/multi.h"
#include "../polclock.h"
#include "../realms/realm.h"
#include "../realms/realms.h"
#include "../uworld.h"
#include "testenv.h"

namespace Pol::Testing
{
void decay_test()
{
  using namespace std::chrono_literals;
  auto createitem = []( Core::Pos4d p, u32 decay )
  {
    auto item = Items::Item::create( 0x0eed );
    item->setposition( p );
    Core::add_item_to_world( item );
    item->set_decay_after( decay );
  };
  auto createmulti = []( Core::Pos4d p, u32 objtype )
  {
    const auto& id = Items::find_itemdesc( objtype );
    auto* multi = Multi::UMulti::create( id );
    multi->setposition( p );
    Core::add_multi_to_world( multi );
    return multi;
  };
  auto decay_full_realm_loop = []( Core::Decay& d )
  {
    do
    {
      d.step();
    } while ( d.area_itr != d.area.begin() );
  };
  INFO_PRINTLN( "    create items" );
  auto* firstrealm = Core::gamestate.Realms[0];
  auto* secondrealm = Core::gamestate.Realms[1];

  // create 3 items, two should decay
  createitem( { 0, 0, 0, firstrealm }, 1 );
  createitem( { 0, 0, 0, firstrealm }, 60 );
  createitem( { firstrealm->area().se() - Core::Vec2d( 1, 1 ), 0, firstrealm }, 1 );
  if ( firstrealm->toplevel_item_count() != 3 )
  {
    INFO_PRINTLN( "first realm toplevelcount 3!={}", firstrealm->toplevel_item_count() );
    UnitTest::inc_failures();
    return;
  }

  // on the second realm one item should also decay
  createitem( { 0, 0, 0, secondrealm }, 1 );
  if ( secondrealm->toplevel_item_count() != 1 )
  {
    INFO_PRINTLN( "second realm toplevelcount 1!={}", secondrealm->toplevel_item_count() );
    UnitTest::inc_failures();
    return;
  }

  // time machine
  Core::shift_clock_for_unittest( 2s );

  // decay thread doesnt run in test environment
  // to be able to test realm add/delete the gamestate instance needs to be used
  auto& d = Core::gamestate.decay;
  d.calculate_sleeptime();
  // first step will move onto first realm from uninitalized state
  d.step();

  // first step should directly destroy on item
  INFO_PRINTLN( "    first zone sweep" );
  d.step();
  if ( firstrealm->toplevel_item_count() != 2 )
  {
    INFO_PRINTLN( "first decay step did not destroy item toplevelcount 2!={}",
                  firstrealm->toplevel_item_count() );
    UnitTest::inc_failures();
    return;
  }
  // since we did already one step, this loop will also switch realm
  INFO_PRINTLN( "    full sweep" );
  decay_full_realm_loop( d );
  if ( firstrealm->toplevel_item_count() != 1 )
  {
    INFO_PRINTLN( "toplevelcount after complete loop 1!={}", firstrealm->toplevel_item_count() );
    UnitTest::inc_failures();
    return;
  }
  if ( d.realm_index != 1 )
  {
    INFO_PRINTLN( "active realm didnt switch 1!={}", d.realm_index );
    UnitTest::inc_failures();
    return;
  }
  // since switching realms is standalone step now, we need step again to clear 0,0
  d.step();
  if ( secondrealm->toplevel_item_count() != 0 )
  {
    INFO_PRINTLN( "second realm toplevelcount 0!={}", secondrealm->toplevel_item_count() );
    UnitTest::inc_failures();
    return;
  }
  // loop until active realm should be again 0
  INFO_PRINTLN( "    rollover" );
  decay_full_realm_loop( d );
  if ( d.realm_index != 0 )
  {
    INFO_PRINTLN( "active realm didnt roll over 0!={}", d.realm_index );
    UnitTest::inc_failures();
    return;
  }

  // test realm add/delete
  INFO_PRINTLN( "    prepare shadow realms" );

  Core::add_realm( "firstshadow", firstrealm );
  Core::add_realm( "secondshadow", firstrealm );
  Core::add_realm( "thirdshadow", firstrealm );
  auto* firstshadow = Core::gamestate.Realms[2];
  auto* secondshadow = Core::gamestate.Realms[3];
  auto* thirdshadow = Core::gamestate.Realms[4];
  thirdshadow->has_decay = false;
  // second shadow realm one item should decay normally
  // one item inside a multi with decay enabled
  // one item inside a multi with decay disabled
  createitem( { 0, 0, 0, secondshadow }, 1 );
  auto* multi = createmulti( { 100, 0, 0, secondshadow }, 0x12000 );
  createmulti( { 200, 0, 0, secondshadow }, 0x12000 );
  if ( !multi )
  {
    INFO_PRINTLN( "failed to create multi" );
    UnitTest::inc_failures();
    return;
  }
  multi->items_decay( true );
  // item inside multi which has decay enabled
  createitem( { 100, 0, 0, secondshadow }, 1 );
  // item inside multi which has decay disabled
  createitem( { 200, 0, 0, secondshadow }, 1 );
  if ( secondshadow->toplevel_item_count() != 3 )
  {
    INFO_PRINTLN( "second shadow toplevelcount 3!={}", secondshadow->toplevel_item_count() );
    UnitTest::inc_failures();
    return;
  }
  // third shadow realm - create one item for decay, but it shouldn't as decay is disabled
  createitem( { 0, 0, 0, thirdshadow }, 1 );
  if ( thirdshadow->toplevel_item_count() != 1 )
  {
    INFO_PRINTLN( "third shadow toplevelcount 1!={}", thirdshadow->toplevel_item_count() );
    UnitTest::inc_failures();
    return;
  }
  // time machine
  Core::shift_clock_for_unittest( 2s );
  // current index is 0 so loop 2 times to get to the first shadow realm
  decay_full_realm_loop( d );
  decay_full_realm_loop( d );
  if ( d.realm_index != 2 )
  {
    INFO_PRINTLN( "active realm isnt first shadow 2!={}", d.realm_index );
    UnitTest::inc_failures();
    return;
  }

  INFO_PRINTLN( "    remove active realm" );
  Core::remove_realm( firstshadow->name() );
  // removing active realm will put us at the end of previous realm making the next step a
  // realm switch
  d.step();
  if ( d.realm_index != 2 )
  {
    INFO_PRINTLN( "active realm isnt second shadow 2!={}", d.realm_index );
    UnitTest::inc_failures();
    return;
  }
  decay_full_realm_loop( d );
  if ( secondshadow->toplevel_item_count() != 1 )
  {
    INFO_PRINTLN( "second shadow toplevelcount 1!={}", secondshadow->toplevel_item_count() );
    UnitTest::inc_failures();
    return;
  }

  if ( d.realm_index != 3 )
  {
    INFO_PRINTLN( "active realm isnt third shadow 3!={}", d.realm_index );
    UnitTest::inc_failures();
    return;
  }
  decay_full_realm_loop( d );
  // item has decay, but realm decay is disabled so it shouldn't disappear
  if ( thirdshadow->toplevel_item_count() != 1 )
  {
    INFO_PRINTLN( "third shadow toplevelcount 1!={}", thirdshadow->toplevel_item_count() );
    UnitTest::inc_failures();
    return;
  }

  UnitTest::inc_successes();
}
}  // namespace Pol::Testing
