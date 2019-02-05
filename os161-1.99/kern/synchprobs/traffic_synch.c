#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */

/* note: this typedef is defined here because it is used only
   within this file, and is not exposed externally */

typedef struct Vehicle
{
  Direction origin;
  Direction destination;
} Vehicle;


// structure defined and use internally
static struct cv *intersectionCv;
static struct lock *intersectionLock;


// think about how to add volatile later on
struct array *vehiclesAtIntersection;

// function defined and use internally
static bool right_turn(Vehicle *vehicle);
static bool areTwoVehiclesSafe(Vehicle *vehicle1,Vehicle *vehicle2);
static bool canVehicleEnterIntersection(Vehicle *vehicle, struct array *vehiclesAtIntersection);
static void removeVehicleFromIntersection(Direction origin, Direction destination, struct array *vehiclesAtIntersection);
/*
 * bool right_turn()
 * 
 * Purpose:
 *   predicate that checks whether a vehicle is making a right turn
 *
 * Arguments:
 *   a pointer to a Vehicle
 *
 * Returns:
 *   true if the vehicle is making a right turn, else false
 *
 * Note: written this way to avoid a dependency on the specific
 *  assignment of numeric values to Directions
 */
static bool
right_turn(Vehicle *v) {
  KASSERT(v != NULL);
  if (((v->origin == west) && (v->destination == south)) ||
      ((v->origin == south) && (v->destination == east)) ||
      ((v->origin == east) && (v->destination == north)) ||
      ((v->origin == north) && (v->destination == west))) {
    return true;
  } else {
    return false;
  }
}



static bool areTwoVehiclesSafe(Vehicle *vehicle1,Vehicle *vehicle2){
  if (vehicle1->origin == vehicle2->origin) {
    return true;
  } else if (vehicle1->origin == vehicle2->destination && vehicle1->destination == vehicle2->origin) {
    return true;
  } else if ((vehicle1->destination != vehicle2->destination) && ((right_turn(vehicle1)) || (right_turn(vehicle2)))) {
    return true;
  }
  return false;
}

/*
 
 Purpose:
  predicate that if a vehicle can enter the intersection
  
 Arguments:
  a pointer to a vehicle
  a pointer to a struct array which contains 

  
  Return:
    true if the vehicle can enter the intersection, else false
*/

static bool canVehicleEnterIntersection(Vehicle *vehicle, struct array *vehiclesAtIntersection) {
  unsigned int lengthOfArray = array_num(vehiclesAtIntersection);

  // check if the vehicle violate rules against any vehicle at the intersection (critical section)
  for (unsigned int i = 0; i < lengthOfArray; ++i) {
    Vehicle *vehicleAtIntersection = array_get(vehiclesAtIntersection,i);
    if (!(areTwoVehiclesSafe(vehicle, vehicleAtIntersection))) {
      return false;
    }
  }

  return true;
}



static void removeVehicleFromIntersection(Direction origin, Direction destination, struct array *vehiclesAtIntersection) {
  unsigned int lengthOfArray = array_num(vehiclesAtIntersection);
  for (unsigned int i = 0; i < lengthOfArray; ++i) {
    Vehicle *vehicleAtIntersection = array_get(vehiclesAtIntersection,i);
    if ((origin == vehicleAtIntersection->origin) && (destination == vehicleAtIntersection->destination)){
      array_remove(vehiclesAtIntersection, i);
    }
    break;
  }
}

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  intersectionLock = lock_create("intersectionLock");
  intersectionCv = cv_create("intersectionCv");

  KASSERT(!(intersectionLock == NULL));
  KASSERT(!(intersectionCv == NULL));
  // create an array for all vehicles at the intersection so that we can add and remove vehicle
  // from this array later on according to the condition
  vehiclesAtIntersection = array_create();
  // vehiclesAtIntersection->num = vehiclesAtIntersection->max =0;
  // vehiclesAtIntersection->v=NULL;
  array_init(vehiclesAtIntersection);
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(!(intersectionLock == NULL));
  KASSERT(!(intersectionCv == NULL));

  // destroy lock, cv, and vehicle at the intersection
  lock_destroy(intersectionLock);
  cv_destroy(intersectionCv);
  array_destroy(vehiclesAtIntersection);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) {
  KASSERT(!(intersectionLock == NULL));
  KASSERT(!(intersectionCv == NULL));

  // create a vehicle
  Vehicle *vehicle = kmalloc(sizeof(struct Vehicle));
  vehicle->origin = origin;
  vehicle->destination=destination;

  kprintf("before acuqire lock\n");
  lock_acquire(intersectionLock);
  kprintf("after acuqire lock\n");
  while (!(canVehicleEnterIntersection(vehicle, vehiclesAtIntersection))) {
    kprintf("before cv_wait\n");
    cv_wait(intersectionCv, intersectionLock);
    kprintf("after cv_wait\n");
  }

  // add vehicle to the vehiclesAtIntersection
  array_add(vehiclesAtIntersection, vehicle, NULL);
  //kprintf("before lock_release\n");
  lock_release(intersectionLock);
  //kprintf("after lock_release\n");

}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) {
  KASSERT(!(intersectionLock == NULL));
  KASSERT(!(intersectionCv == NULL));
  //kprintf("before intersection_after_exit lock_acquire\n");
  lock_acquire(intersectionLock);
  //kprintf("after intersection_after_exit lock_acquire\n");
  removeVehicleFromIntersection(origin, destination, vehiclesAtIntersection);
  //kprintf("before intersection_after_exit cv_broadcast\n");
  cv_broadcast(intersectionCv, intersectionLock);
  //kprintf("after intersection_after_exit cv_broadcast\n");
  //kprintf("before intersection_after_exit lock_release\n");
  lock_release(intersectionLock);
  //kprintf("after intersection_after_exit lock_release\n");

}
