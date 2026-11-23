# **scratchpad**

(recurring) parse through of entire codebase to find bad practices, dead code, contradictory code etc. The code should be clean, because we are trying to rely less on documentation, and have code as documentation so the LLMs coding dont do things we dont want because of some bad code precedence.

## **Wishlist before next release - G++ Pro Edition**

- Chipping
- more courses - a lot of famous danish, and get american imports working to get a ton of american courses.
- Teepads - prettier, no teepads on real courses,
- Scrambles, best ball, closest to the pin, mulligans, pick up and drop ball rules.
- background from sorrounding area on the map? would be so cool
- weird white and blue lines on the edge of the ground when sun's hitting just right.
- The ui-text shouldn't change with the resolution.
- the flight line should still be drawn when you exit the following cam by moving.
- better fairway drawing and tee placements in the importer? I think especially horsens par 18 is a good example of some of the holes im sure are wrong.
- some sound for missing stuff like crashes, and maybe some volume mixing, like playing the different sounds at different volumes by default. maybe a mixer.json or something.
- fucked up names for every course so I can make this alpha version "public"-ish? Making a website to release it on, and making the repo private
- remove fine? I dont think i like it haha it makes the difference between the fidelity of assets too jarring
- clean the build folder
- Starting a shot shouldn't point at the flag its cheating.
- should teetimes be local time?
- F1 and F2 menus authorization?
- error messages only for error message - move loading screens to and fro courses, ball dropping, +x for water or hitting stuff to other places
- does cart path affect the ball differently? It should
- the hub needs one more run through - the teepads, the clubhouse.
- clean up ui? the date and shit on the screen, the club selection, the club display.

## **Todo Agent Setup rules**

- Its important for me with the seggregation of stuff like what draws assets, animations, sound etc to always be explicit, so its like a shell that can be changed later.
- UI should be minimal - In the perfect world we would have none. Time would be a watch and an animation to look at it, ui should be hidden in real world objects like the wind in the rangefinder or the particles.
- Error messages should only ever appear if an actual error is present.
- Game philosophy: Just like the UI, I want the game to model real life and interactibility as its pillars. If any design choice come up, either ask or make the choice by asking
        1. How Is this/golf in real life?
        2. How do we best let the player interact with this thing, so it feels real
- Performance is key, if something undermines performance it should be togglable, like the lighting enginge i guess.

**Workflow**
I want the agents to be able to use worktrees, branches and pull requests.
I want an agent constantly looking at the todo/, and the open issues, then branches and pull requests from them.
- I am very particular about the game design for this project: its the only hope for this game to have character, when its a vibe coded lofi game. I am very often finding that the models I'm using have to, and are making design decisions, even when I ask them to ask me for anything undefined. at some level i expect this will always be the case, so we need some design guidelines that capture the essence of the game direction, so fewer wrong design choices will be made.
  1. UI should have some guidelines, that I think should use especially the rangefinder as examples, UI that is not intrusive and fits in the game. I want as little UI as possible, and the UI we have I would like to be physical things in the world you interact with. Stuff like the distance numbers that pop up are the exceptions, and are taken from the MMORPG side of the game, but most of the UI should be "as little as possible". Also similar UI elements like Error messages should have a general and consistent look and display, which they currently dont.
  2. For game design choices, the general guideline should be realism first. For any design decision, ask yourself "how would this work in real life, and whats the easiest abstraction of this in code that would make it feel like that in game, without sacrificing UX too much." Worse UX for more realism is often preferred, like walking to your bag to get a club.
  3. The project is now more mature, and would benefit from worktrees and multiple agents, so we need a more mature workflow. Help me design such guidelines.
There are also some guidelines like the file-length, that I am unsure whether if healthy - are we just splitting files to split them? How do we design more amture guidelines like this for 
We also need some new guidelines for testing - it keeps growing (which I guess is expected) but often hasn't tested what I would say is "expected behaviour" It often seems its testing the code is the code type of thing. How do we test behaviour instead? I am also using a bunch of time on actual in game testing, so more minimal testing might also be okay? I don't know, but it needs to be looked at.
This should take precedent over basically every other readme!

## **Bugs**

- The headcovers are ugly as hell


## **alpha roadmap**
Each of these could be a new 0.x release. Bugs are 0.0.x releases, but probably wont be released unless they are gamebreaking. Find as many as make sense, then release alpha 1.0. 
After that the game will enter beta where it should mostly be balance, changes from feedback, and creating content - more skills, more levelup rewards from skills, importing and testing more courses, more things to buy from the shop, more ways to earn money and exp. 
When the game feels balanced, full and fun, release :D


2. **real skills and leveling system - think in unlocks, but wait for shops**

!!!! Simulator! Simple hub world that just consists of these and like a bus where you choose where to go.

- Skills in **driver**, **woods**, **hybrids**, **long irons**, **short irons**, **wedges**.
Your ability to shoot the different clubs - chance to miss, withd hit range.
Get exp each shot based on how you do - a longer putt that goes in gives a lot, a putt that goes past gives less. A topped driver gives less, etc. Proximity to the hole vs your starting proximity. Going in the water gives nothing, going in bunkers gives less, hitting a green gives more.
- **Smoking**, **drinking**
better smokes and drinks. Im thinking something like a cigarette giving much more exp but also smoking for a longer time so you have to press less etc. More expensive but stronger drinks, as a consumable. Maybe taking a sip of a beer depletes 10% of it, you get exp per sip and more when you finish it but you have to replenish them. (comes with shops in 5)
- **fitness**, **driving** 
get fitness from walking, unlocks a quest to unlock driving
Get driving from driving nice (on the paths etc.) maybe get a debuff from crashing/not going on the paths.
- **exploration**
Discovering new courses, finding stuff out in the world
- **social**
Exp from each hole in a group, tournaments and more with a caddy whether required or not,  
Being in a group and training social gives a multiplier on everything else. 
- **caddying**
goes up from caddying. You can always caddy, doesnt have to be a tournament. Some tournaments require a caddy of a certain level. Gives people a multiplier on how well they hit their clubs, you hand people their clubs and always choose what they hit.
- **handicap**
Based on real life - you recieve it when you've gotten your golf card, which you get through the introductory quests. ()

- **rep**
Some sort of way of having a reputation - you lose it by crashing carts, driving on the green, having trolleys go on greens og inbetween greens and bunkers, etc. Everything bad in golf. Maybe only if youre caught for certain things? But then more rep loss if 2, 3 people see you etc. Then some courses have a rep requirement to enter, so you basically lock yourself out of the game from acting bad - but its possible to do.

- **Strength, fade, draw, flop, putting**
Basically all of these will be unlocked through quests, but all will be available in the alpha. Then when quests are added, they will be hidden behind, but dev accounts should be a thing to turn on to always make them available.

  **Handedness and natural shot**
This should go into yur skills. When changing your handedness and natural shot, it should be possible always, but probably be gated behind a quest for natural shot. Your handedness could retain 10% of your total exp from the other hand, and then let you train on top, also increasing your skill with the original handedness. The same for the natural shot - some players change it later, but it will affect your skills.

3. **new menu UIs for everything ingame - map, scorecard, party and friends**  

clubs, gear(cart, smokes, rangefinder etc.), levels, inventory, music, friends&party, scorecard, map. Some cool physical way of getting the stuff out of a bag. Friends, music and map on the phone, clubs, smokes and rangefinder in a bag on your back or in the cart.
A view of how many people are in each course. Just 0/40 -> 40+ if more than one room.
social panel - friends, grouping - by dapping up?

4. **Real visual asset- and sound asset- designer, and real assets.**

Music controlled by the day night cycle and weather system.
parody club brands: Hatermade, Tiltist, Wacomo etc. 


5. **Quests and Shops in Hub (and maybe on each course for random stuff?)**

Move the Course chooser to the hub - busses, planes ships whatever.
Also a driving range / simulator to get a view of how fair your clubs can go.
Quests give exp in skills, unlock skills, give money, or gear(like clubs, cart, rangefinder - but many of these are basically unlocking skills, like e.g. the cart and a driver.)
Money, to be earned somehow - maybe just some repeatable quests called "tournaments" in itis simplest form
Cosmetics in the golf cart rear view mirror
flirting with the cart girl

 6. **To be planned**

- player owned housing - small instances where friends can join and be invited, where you can displayed collectibles, tournament wins and gear like clubsets etc.
- Tournaments, some only enterable with a caddy, which should be a real person. Live and longer timed events (i.e. one entry before thursday.)
- player owned clubs with player made courses - endgame and expensive, revenue from players playing and making tounaments
- having different bags affecting walking speeds differently - i.e. a smaller setup bag for par 3 courses that would allow you to walk faster.
- wildlife, set for each course.
- Active buffs shows, so you can see like how drunks you are, and how other things affect you - e.g. maybe you can be cold / wet, and you can have like a umbrella to lessen the debuff.
- Tapping it in when its close enough? the gameplay is not that fun at very close putts. At least being able to say good-good, or tapping it in when solo?
- In game proximity chat? Would be so fucking sick. Maybe just in parties, but you and a caddie or just a group on proximity ingame would be amazing.

## **story**

### **free tier**

*kind* of linear. You can do whatever you like, but you have to get your license
You start out only being able to play par 3 courses, only in danefield.
You have to get your golf license by getting better at playing, taking some "lesson" tutorial, and unlocking some things like drinking and smoking.
You start with only 7 iron, 9 iron, 56 and a putter, all of some fucked up brand


## **paid tier**

Game open up more
Youll probably want to hit just above 200 with your driver when you enter here, and hit it pretty consistently, scaling up to like probably a good bit north of 300 with gear - but I imagine accuracy will be more sought after in lategame that distance. Having great distance means more distance between clubs
Like runescape it unlocks some abilities like driving, 18 hole real courses, etc.
You can play tournaments, which are a type of quest to unlock specific cooler courses where exp is enhanced.
You also get more exp from playing in a group.
