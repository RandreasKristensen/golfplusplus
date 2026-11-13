#include "doctest.h"
#include "test_support.h"
#include "game/dap_rules.h"
#include "game/social_dap.h"
#include "game/hub_flow.h"
#include "game/interactables.h"
#include "game/online_play.h"
#include "game/progress_rules.h"
#include "game/server_errors.h"
#include "game/text_ids.h"
#include "core/render_frame.h"
#include "renderer/dap_pose.h"
#include "renderer/hud_overlay.h"
#include "net/room_rows.h"
#include <glm/gtc/matrix_transform.hpp>

namespace {
game_state social_game() {
    auto state = started_game(fixture_hub_course());
    state.play = play_mode::online; state.mode = game_mode::walking;
    state.online.room = online_room{7, online_hub_course, 2, 1}; state.online.room_ready = true;
    state.online.account_id = 1; state.online.server_now = 10000000;
    state.player.position = glm::vec3(0); state.player.yaw = 0;
    state.world.collectibles.clear(); state.world.hole_starts.clear(); state.world.hub.reset();
    state.area.bus.reset(); state.bag.where = bag_where::on_ground; state.bag.position = glm::vec3(100);
    state.online.players[1].motion = net_motion{};
    net_motion other; other.position = glm::vec3(0,0,1.2f); other.yaw = glm::pi<float>();
    state.online.players[2].motion = other;
    return state;
}
room_dap paired(const game_state& state) {
    return {9, 1, 2, state.online.server_now, state.player.position, state.online.players.at(2).motion->position};
}
glm::vec2 screen(const glm::vec3& eye, const glm::vec3& front, const glm::vec3& point, const float aspect) {
    const auto matrix = glm::perspective(glm::radians(60.0f), aspect, 0.01f, 100.0f) * glm::lookAt(eye,eye+front,world_up);
    const auto clip = matrix * glm::vec4(point,1);
    CHECK(clip.w > 0.0f);
    return glm::vec2(clip) / clip.w;
}
}
TEST_CASE("dap server boundary rejects self room distance and busy participants") {
    const auto tuning = shipped_content().tuning;
    dap_participant a{1,7,glm::vec3(0),motion_mode::walk,false};
    dap_participant b{2,7,glm::vec3(0,0,tuning.dap.reach_meters),motion_mode::idle,false};
    CHECK(check_dap(a,b,tuning).empty());
    b.position.z += 0.001f; CHECK(check_dap(a,b,tuning)==error_too_far);
    b.position.z = 1; b.position.y = 10; CHECK(check_dap(a,b,tuning)==error_too_far);
    b.position.y = 0; b.account = 1; CHECK(check_dap(a,b,tuning)==error_not_available);
    b.account = 2; b.room = 8; CHECK(check_dap(a,b,tuning)==error_not_in_room); b.room = 7;
    for (const auto mode : {motion_mode::address,motion_mode::swing,motion_mode::cart,motion_mode::ride}) {
        b.mode=mode; CHECK(check_dap(a,b,tuning)==error_not_available);
    }
    b.mode=motion_mode::walk; a.busy=true; CHECK(check_dap(a,b,tuning)==error_not_available);
    a.busy=false; b.busy=true; CHECK(check_dap(a,b,tuning)==error_not_available);
    const std::int64_t start=10000000;
    const auto duration=static_cast<std::int64_t>(tuning.dap.seconds*1000000.0f);
    CHECK(dap_running(start,start,tuning.dap)); CHECK(dap_running(start,start+duration-1,tuning.dap));
    CHECK(!(dap_running(start,start+duration,tuning.dap))); CHECK(!(dap_running(start,start-1,tuning.dap)));
}
TEST_CASE("dap uses nearest F pending acknowledgement refusal and session guards without offline progress") {
    auto state = social_game();
    REQUIRE(nearest_interactable(state)); CHECK(nearest_interactable(state)->kind==interactable_kind::dap);
    state.ball=my_ball{0,glm::vec3(20,0,20),3,ball_place::at_rest,ball_kind::scored};
    state.online.balls[1]=room_ball{0,state.ball->position,3,false,ball_kind::scored};
    const auto revision=state.terrain_render_revision;
    state.online.effects.push_back(active_effect{"boost",{"xp.social",effect_bucket::increased,1.0f,effect_duration::seconds,10},0,100000});
    const auto saved=state.save.skills;
    request_dap(state,2); REQUIRE(state.dap_pending); REQUIRE(!state.net_commands.empty());
    CHECK(state.net_commands.back().type==net_command_type::dap); CHECK(state.net_commands.back().target_account==2);
    CHECK(state.ball->strokes==3); CHECK(state.ball->position==glm::vec3(20,0,20));
    CHECK(state.terrain_render_revision==revision); CHECK(state.online.effects.size()==1);
    CHECK(state.net_commands.back().room_id==7); CHECK(!(nearest_interactable(state)));
    const auto count=state.net_commands.size(); request_dap(state,2); CHECK(state.net_commands.size()==count);
    state.online.refusals.push_back({reducer_dap,error_too_far}); update_online_play(state,0);
    CHECK(!(state.dap_pending)); CHECK(state.save.skills.size()==saved.size()); CHECK(state.online.progress.skills.empty());
    request_dap(state,2); state.online.daps[9]=paired(state); reconcile_dap(state);
    CHECK(!(state.dap_pending)); CHECK(dap_holds_movement(state));
    const auto pose=state.player.position; const auto ball=state.ball; const auto strokes=state.round->strokes;
    game_input input; input.forward_held=true; update_game(state,input,0.05f);
    CHECK(state.player.position==pose); CHECK(state.ball.has_value()==ball.has_value()); CHECK(state.round->strokes==strokes);
    state.online.server_now+=static_cast<std::int64_t>(state.tuning.dap.seconds*1000000.0f);
    CHECK(dap_available(state,2));
    state.hub_flow.ball_request=net_command_type::tee_up; CHECK(!(dap_available(state,2))); state.hub_flow.ball_request.reset();
    state.hub_flow.travel_course="elsewhere"; CHECK(!(active_dap(state,1))); CHECK(!(dap_available(state,2)));
    state.hub_flow.travel_course.clear(); state.play=play_mode::offline; state.net_commands.clear(); request_dap(state,2);
    CHECK(state.net_commands.empty()); CHECK(state.save.skills.size()==saved.size());
}
TEST_CASE("dap paired row is a snapshot with matching partners time and no replay after expiry or teardown") {
    auto first=social_game(); auto second=social_game(); second.online.account_id=2;
    room_rows a,b; server_clock clock;
    a.begin(7,first); b.begin(7,second);
    stdb_row row{}; row.dap={9,7,1,2,10000000,0,0,0,0,0,1.2f};
    stdb_event event{}; event.table=STDB_TABLE_DAP; event.change=STDB_ROW_INSERT; event.row=row;
    CHECK(a.apply(event,1,0,clock,first)); CHECK(b.apply(event,2,0,clock,second));
    a.ready(first); b.ready(second);
    REQUIRE(first.online.daps.size()==1); REQUIRE(second.online.daps.size()==1);
    CHECK(first.online.daps.at(9).second==2); CHECK(second.online.daps.at(9).first==1);
    CHECK(first.online.daps.at(9).started_at==second.online.daps.at(9).started_at);
    first.online.server_now=20000000; CHECK(!(active_dap(first,1)));
    CHECK(first.save.skills.empty()); CHECK(first.online.progress.skills.empty());
    event.change=STDB_ROW_DELETE; a.apply(event,1,0,clock,first); a.publish(first); CHECK(first.online.daps.empty());
    b.clear(second); CHECK(second.online.daps.empty()); CHECK(!(second.dap_pending));
    event.row.dap.room_id=8; a.apply(event,1,0,clock,first); a.publish(first); CHECK(first.online.daps.empty());
}
TEST_CASE("dap pending partner departure timeout and travel always release presentation") {
    auto state=social_game(); request_dap(state,2); state.online.players.erase(2); reconcile_dap(state); CHECK(!(state.dap_pending));
    state=social_game(); request_dap(state,2); state.online.server_now+=10000000; reconcile_dap(state); CHECK(!(state.dap_pending));
    state=social_game(); state.online.daps[9]=paired(state); REQUIRE(active_dap(state,1));
    state.online.players.erase(2); CHECK(!(active_dap(state,1)));
    clear_online_presentation(state); CHECK(state.online.daps.empty());
}
TEST_CASE("dap Social award uses each recipient's own effective XP and keeps input progress immutable") {
    const auto reward=shipped_content().rewards.dap;
    CHECK(reward.skill_id=="social"); CHECK(reward.xp>0);
    save_data before;
    active_effect boost{"test", {"xp.social",effect_bucket::increased,1.0f,effect_duration::seconds,10},0,10000};
    const auto first=award_xp(before,reward,{{boost},1000});
    const auto second=award_xp(before,reward,{{},1000});
    REQUIRE(first.awarded.size()==1); REQUIRE(second.awarded.size()==1);
    CHECK(first.awarded[0].xp==reward.xp*2); CHECK(second.awarded[0].xp==reward.xp);
    CHECK(before.skills.empty());
    CHECK(dap_award_key(9,1)=="dap:9:1");
    CHECK(dap_award_key(9,1)!=dap_award_key(9,2));
    CHECK(dap_award_key(9,1)!=dap_award_key(10,1));
}
TEST_CASE("dap fists project into both first-person views and face the common contact for either hand and yaw") {
    const auto tuning=shipped_content().tuning;
    const float distance=1.2f;
    for (const auto yaw : {0.0f,1.2f,-2.1f}) {
        const auto front=yaw_direction(yaw);
        const room_dap pair{9,1,2,10000000,glm::vec3(0),front*distance};
        const auto now=pair.started_at+static_cast<std::int64_t>(tuning.dap.seconds*tuning.dap.contact_share*1000000.0f);
        for (const auto hand1 : {handedness::left,handedness::right}) for (const auto hand2 : {handedness::left,handedness::right}) {
            const auto a=paired_dap_pose(pair,1,hand1,now,tuning);
            const auto b=paired_dap_pose(pair,2,hand2,now,tuning);
            CHECK(near(glm::distance(a.hand,b.hand),2*a.radius));
            CHECK(glm::dot(a.front,front)>0.99f); CHECK(glm::dot(b.front,front)<-0.99f);
            const auto at_start=paired_dap_pose(pair,1,hand1,pair.started_at,tuning);
            CHECK(glm::dot(at_start.hand,yaw_left(front))*(hand1==handedness::left?1.0f:-1.0f)>0);
            for (const auto aspect : {4.0f/3.0f,16.0f/9.0f,21.0f/9.0f}) {
                for (const bool first : {true,false}) {
                    const auto eye=(first?pair.first_position:pair.second_position)+world_up*tuning.camera.walking_eye_height;
                    const auto facing=first?front:-front;
                    const auto mine=screen(eye,facing,first?a.hand:b.hand,aspect);
                    const auto theirs=screen(eye,facing,first?b.hand:a.hand,aspect);
                    CHECK(std::abs(mine.x)<0.05f); CHECK(mine.y<0); CHECK(mine.y>-1);
                    CHECK(std::abs(theirs.x)<0.05f); CHECK(theirs.y<0); CHECK(theirs.y>-1);
                    const auto knuckles=screen(eye,facing,(first?a.hand:b.hand)+(first?a.front:b.front)*a.radius,aspect);
                    CHECK(knuckles.y>mine.y); // a reversed fist points towards the camera and fails
                }
            }
        }
    }
}
TEST_CASE("dap hint and Social icon and XP display fit supported aspect ratios") {
    auto game=social_game(); game.skills_panel_active=true;
    game.online.progress.skills["social"].xp=50;
    refresh_static_anchor_cache(game);
    const auto data=make_render_data(game,shipped_text_assets(),shipped_content().skills,render_meshes{},test_props(),60,true);
    REQUIRE(data.key_hints); CHECK(std::string(data.key_hints)==text_dap_hint);
    for (const overlay_grid grid : {overlay_grid{640,480},overlay_grid{854,480},overlay_grid{1120,480}}) {
        overlay_batch batch; batch.grid=grid; draw_hud(batch,shipped_text_assets(),data,glm::mat4(1));
        CHECK(batch.truncated_text_count==0);
    }
}

TEST_CASE("dap assembled local partner and observer frames use the same server timeline") {
    auto game=social_game(); game.online.daps[9]=paired(game);
    game.online.server_now+=700000;
    game.hand=handedness::left; game.online.players[1].hand=handedness::left;
    game.remote_avatars[2].position=game.online.players[2].motion->position;
    game.remote_avatars[2].mode=motion_mode::cart; // a stale presentation must yield to the accepted on-foot pair
    refresh_static_anchor_cache(game);
    const auto frame=make_render_data(game,shipped_text_assets(),game.skills,render_meshes{},test_props(),60,true);
    REQUIRE(frame.dap_hand); REQUIRE(frame.remote_avatars.size()==1); REQUIRE(frame.remote_avatars[0].dap);
    CHECK(!frame.remote_avatars[0].seated);
    REQUIRE(frame.my_figure); CHECK(frame.my_figure->reach==std::optional<glm::vec3>(frame.dap_hand->hand));
    CHECK(frame.remote_avatars[0].reach==std::optional<glm::vec3>(frame.remote_avatars[0].dap->hand));
    CHECK(near(glm::distance(frame.dap_hand->hand,frame.remote_avatars[0].dap->hand),2*frame.dap_hand->radius));
    auto before=game; before.online.daps.clear();
    const auto unchanged=make_render_data(before,shipped_text_assets(),game.skills,render_meshes{},test_props(),60,true);
    CHECK(frame.camera_position==unchanged.camera_position);
    CHECK(frame.camera_target==unchanged.camera_target);
    auto partner=game; partner.online.account_id=2; partner.hand=handedness::right;
    partner.player.position=game.online.players[2].motion->position;
    partner.player.yaw=glm::pi<float>(); partner.remote_avatars.clear(); partner.remote_avatars[1].position=glm::vec3(0);
    const auto other=make_render_data(partner,shipped_text_assets(),game.skills,render_meshes{},test_props(),60,true);
    REQUIRE(other.dap_hand); REQUIRE(other.remote_avatars.size()==1); REQUIRE(other.remote_avatars[0].dap);
    CHECK(other.dap_hand->hand==frame.remote_avatars[0].dap->hand);
    CHECK(other.remote_avatars[0].dap->hand==frame.dap_hand->hand);
    auto observer=game; observer.online.account_id=3; observer.remote_avatars[1].position=glm::vec3(0);
    const auto seen=make_render_data(observer,shipped_text_assets(),game.skills,render_meshes{},test_props(),60,true);
    CHECK(!(seen.dap_hand)); REQUIRE(seen.remote_avatars.size()==2);
    REQUIRE(seen.remote_avatars[0].dap); REQUIRE(seen.remote_avatars[1].dap);
    CHECK(seen.remote_avatars[0].dap->hand==frame.dap_hand->hand);
    CHECK(seen.remote_avatars[1].dap->hand==frame.remote_avatars[0].dap->hand);
}
TEST_CASE("dap reward loader requires a positive reward and a listed earned Social skill") {
    const std::string base=R"({"shot_xp":{"skill":"golf_swing","xp":25},"movement_xp":{"walking":{"skill":"fitness","meters_per_xp":10},"cart_on_road":{"skill":"cart_driving","meters_per_xp":20}})";
    CHECK(!(parse_rewards_from_text(base+"}").has_value()));
    CHECK(!(parse_rewards_from_text(base+R"(,"dap_xp":{"skill":"social","xp":0}})").has_value()));
    CHECK(parse_rewards_from_text(base+R"(,"dap_xp":{"skill":"social","xp":10}})").has_value());
    CHECK(!(parse_rewards_from_text(base+R"(,"dap_xp":{"skill":"","xp":10}})").has_value()));
    auto skills=shipped_content().skills; auto rewards=shipped_content().rewards;
    for(auto& skill:skills.skills) { if(skill.id=="social") { skill.retired=true; } }
    CHECK(unpayable_skill(skills,rewards,game_state{}.items)=="social");
}
