#include "cht.h"
#include "cht_message.h"
#include <assert.h>
#include <string.h>
#include <netinet/in.h>
/*#define setup_vars                                                                          \
TwoPCAppState * app_state_2pc = ((TwoPCAppState*)(((DfaSet*)dfaSet)->app_setup));           \
CHTAppState * app_state_cht = app_state_2pc->app_state;                                     \
jbHashTable_t * xid_ht = app_state_cht->xid_ht;                                             \
jbHashTable_t * ht_ht = app_state_cht->ht_ht;                                               \
int ht_xid = app_state_cht->ht_xid;                                                         \
int xid;                                                                                    \
int xid_exists = (-1 != jbHtLookup(ht_xid, xid_ht,(byte*) &(stateMachine->machine_id), sizeof(state_machine_id), (byte*)&xid)); \
jbHashTable_t ht;                                                                                    \
int ht_exists = (-1 != jbHtLookup(ht_xid, ht_ht, (byte*)&(__header_ptr(m)->hashTable), sizeof(clusterHashTable_t),(byte*) &ht))*/
#define setup_vars                                                                          \
TwoPCAppState * app_state_2pc = ((TwoPCAppState*)(((DfaSet*)dfaSet)->app_setup));           \
CHTAppState * app_state_cht = app_state_2pc->app_state;                                     \
recordid xid_ht = app_state_cht->xid_ht;                                             \
recordid ht_ht = app_state_cht->ht_ht;                                               \
int ht_xid = app_state_cht->ht_xid;                                                         \
int xid;                                        \
int * xid_ptr = 0;                                                                                 \
int xid_exists = (-1 != ThashLookup(ht_xid, xid_ht,(byte*) &(stateMachine->machine_id), sizeof(state_machine_id), (byte**)&xid_ptr)); \
if(xid_ptr) { xid=*xid_ptr; free(xid_ptr); }\
recordid ht;                                                                                    \
recordid * ht_ptr = 0;                                         \
int ht_exists = (-1 != ThashLookup(ht_xid, ht_ht, (byte*)&(__header_ptr(m)->hashTable), sizeof(clusterHashTable_t),(byte**) &ht_ptr)); \
if(ht_ptr) { ht=*ht_ptr; free(ht_ptr) ;}

/** TODO For now, we ignore the possiblity that jbHashTable's functions
    return error codes.  Instead, we assume that they always
    succeed. */
static state_name do_work(void * dfaSet, StateMachine * stateMachine, Message * m, char * from) {

  setup_vars;
  int ret = 1;  
  switch(*requestType(m)) 
    {  
    case CREATE: 
      {
	recordid new = ThashCreate(ht_xid, VARIABLE_LENGTH, VARIABLE_LENGTH);
	//	jbHashTable_t * new = jbHtCreate(ht_xid, 79);
	//	if(new != NULL) {
	ThashInsert(ht_xid, ht_ht, 
		    (byte*)&(__header_ptr(m)->hashTable), sizeof(clusterHashTable_t), 
		    (byte*)&new, sizeof(recordid));
	//	  ret = (jbHtInsert(ht_xid, ht_ht, (byte*)&(__header_ptr(m)->hashTable), sizeof(clusterHashTable_t), (byte*)new, sizeof(jbHashTable_t)) >= 0);
	//	} else {
	//	  ret = 0;
	//	}
	//	if(ret) {
	printf("Created local slice of global hash table %d\n", (__header_ptr(m)->hashTable)); fflush(stdout);
	Tcommit(app_state_cht->ht_xid);
	app_state_cht->ht_xid = Tbegin();	  
	//	} else {
	//	  printf("Failed to insert new hash table slice!");
	//	}
	
      } break;
      
    case INSERT: 
      {
	if(!ht_exists) {
	  printf ("Hash table %d doesn't exist!\n", (__header_ptr(m)->hashTable)); fflush(stdout); ret = 0; 
	} else {
	  //ret = (jbHtInsert(xid, &ht, getKeyAddr(m), getKeyLength(m), getValAddr(m), getValLength(m)) >= 0);
	  ThashInsert(xid, ht, getKeyAddr(m), getKeyLength(m), getValAddr(m), getValLength(m));
	  printf("Insert: %d ht=%d (key length %d) %d -> %d\n", ret, 
		 (__header_ptr(m)->hashTable), getKeyLength(m), 
		 *(int*)getKeyAddr(m), *(int*)getValAddr(m));
	  fflush(stdout);
	  
	  //	  (jbHtInsert(ht_xid, ht_ht, (byte*)&(__header_ptr(m)->hashTable), sizeof(clusterHashTable_t), (byte*)&ht, sizeof(jbHashTable_t)));
	  
	}
      } break;
      
    case LOOKUP:
      {
	if(!ht_exists) { 
	  printf ("Hash table doesn't exist!\n"); fflush(stdout); ret = 0; 
	} else {
	  byte * new;
	  int valueLength = ThashLookup(xid, ht, getKeyAddr(m), getKeyLength(m), &new);
	  if(valueLength != -1) {
	    assert(valueLength <= getValLength(m));
	    memcpy(getValAddr(m), new, valueLength);
	    free(new);
	  } else {
	    ret = 0;
	  }
	  //	  ret = (jbHtLookup(xid, &ht, getKeyAddr(m), getKeyLength(m), getValAddr(m)) >= 0);
	  printf("Lookup: %d ht=%d (key length %d) %d -> %d\n", ret, 
		 (__header_ptr(m)->hashTable), getKeyLength(m),
		 *(int*)getKeyAddr(m), *(int*)getValAddr(m));
	  fflush(stdout);
	} 
      } break;
      
    case REMOVE:
      {
	if(!ht_exists) { 
	  printf ("Hash table doesn't exist!\n"); fflush(stdout); ret = 0; 
	} else {
	  /** @todo no longer return old value on remove... */
	  ret = ThashRemove(xid, ht, getKeyAddr(m), getKeyLength(m));
	  //	  ret = (jbHtRemove(xid, &ht, getKeyAddr(m), getKeyLength(m), getValAddr(m)) >= 0);
	  //  (jbHtInsert(ht_xid, ht_ht, (byte*)&(__header_ptr(m)->hashTable), sizeof(clusterHashTable_t), (byte*)&ht, sizeof(jbHashTable_t)));
	}
      } break;
      
    case DELETE: 
      {
	if(!ht_exists) { printf ("Hash table doesn't exist!\n"); fflush(NULL); ret = 0; } else {
	  ThashRemove(xid, ht_ht, getKeyAddr(m), getKeyLength(m));
	  //	  jbHtRemove(xid, ht_ht, getKeyAddr(m), getKeyLength(m), NULL);
	  //	  (jbHtInsert(ht_xid, ht_ht, (byte*)&(__header_ptr(m)->hashTable), sizeof(clusterHashTable_t), (byte*)&ht, sizeof(jbHashTable_t)));
	  /*	  ret = (jbHtDelete(xid, &ht) >= 0); */ /* Don't need this--jbHtDelete just frees the (stack!) pointer. */
	  Tcommit(app_state_cht->ht_xid);
	  app_state_cht->ht_xid = Tbegin();	  

	}
      } break;
      
    case TSTSET: 
      {
	printf("Unimplemented request!\n");
      } break;
    
    case GETXID: 
      {
	/*	int new_xid = Tbegin();
	if(new_xid >= 0) {
	  setKeyLength(m, 0);
	  setValLength(m, sizeof(int));
	  *((int*)getValAddr(m)) = new_xid;
	  ret = 1;
	  if(jbHtInsert(ht_xid, xid_ht, &(stateMachine->machine_id), sizeof(state_machine_id), &xid, sizeof(int)) == -1) {
	    printf("Begin failed on jbHtInsert!\n");
	  } else {
	    printf("Created local xid for global xid: %ld\n", stateMachine->machine_id);
	  }
	} else {
	  printf("Begin failed on Tbegin()!\n");

	  ret = 0;
	  }  NOOP */
      } break;
      
      /*    case COMMIT: 
      {
	ret = (Tcommit(xid) >= 0);
      } break;
      
    case ABORT: 
      {
	ret = (Tabort(xid) >= 0);
      } break;
      */
    default: 
      {
	printf("Unknown request type: %d\n", *requestType(m));
      }
    }
  
  return ret;
}

/* Run by the coordinator when the request is received from the client. */
state_name init_xact_cht(void * dfaSet, StateMachine * stateMachine, Message * m, char * from) {

  TwoPCAppState * app_state_2pc = ((TwoPCAppState*)(((DfaSet*)dfaSet)->app_setup));           
  CHTAppState * app_state_cht = app_state_2pc->app_state;                                     

  if(m->type != m->response_type) {
    printf("Bug in client!  m->type != response_type(m).\n");
  }

  if(*requestType(m) == CREATE) {
    clusterHashTable_t new_cht;
    new_cht.id = app_state_cht->next_hashTableId;
    app_state_cht->next_hashTableId++;

    memcpy(&(__header_ptr(m)->hashTable), &new_cht, sizeof(clusterHashTable_t));

    printf("Allocated hashtable %d\n", new_cht.id);
  }

  printf("requestType: %d, responseType: %d key: %d from %s:%ld\n", *requestType(m), m->response_type, *(int*)getKeyAddr(m), m->initiator, m->initiator_machine_id);

  return 1;
}

/* Begins new transaction, does the work the transaction requests, stores the message, and returns the corresponding error code. */
state_name veto_or_prepare_cht(void * dfaSet, StateMachine * stateMachine, Message * m, char * from) {

  state_name ret;
  setup_vars;

//  int xid_exists = (-1 != jbHtLookup(ht_xid, app_state_cht->xid_ht, &(stateMachine->machine_id), sizeof(state_machine_id, &xid));
  
  if(xid_exists) { printf("Warning:  Stale xid found!\n"); }
  assert(!xid_exists);
  printf("requestType: %d, responseType: %d key: %d from %s:%ld\n", *requestType(m), m->response_type, *(int*)getKeyAddr(m), m->initiator, m->initiator_machine_id);

  /* This is the start of a new transaction */
  xid = Tbegin();  // !!!!
  if(xid < 0) {

    printf("Tbegin failed; %d\n", xid);

    //  } else if(jbHtInsert(ht_xid, xid_ht, (byte*)&(stateMachine->machine_id), sizeof(state_machine_id), (byte*)&xid, sizeof(int)) == -1) {
  } else {
    ThashInsert(ht_xid, xid_ht, (byte*)&(stateMachine->machine_id), sizeof(state_machine_id), (byte*)&xid, sizeof(int));
    xid_exists = 1;
  }
  Tcommit(app_state_cht->ht_xid);
  app_state_cht->ht_xid = Tbegin();
  
  if(xid_exists) {
    
    ret = do_work(dfaSet, stateMachine, m, from);
    
    ret = ret ? SUBORDINATE_PREPARED_2PC : SUBORDINATE_VETO_2PC;

  } else {

    ret = SUBORDINATE_VETO_2PC;

  }

  if(ret == SUBORDINATE_VETO_2PC) {
    abort_cht(dfaSet, stateMachine, m, from);
  }
  return ret;
}

state_name abort_cht(void * dfaSet, StateMachine * stateMachine, Message * m, char * from) {
  setup_vars;

  printf("Aborting!!\n");

  assert(xid_exists);

  Tabort(xid); // !!!!
  //  jbHtRemove(ht_xid, xid_ht, (byte*)&(stateMachine->machine_id), sizeof(state_machine_id), (byte*)&xid);
  ThashRemove(ht_xid, xid_ht, (byte*)&(stateMachine->machine_id), sizeof(state_machine_id));
  Tcommit(app_state_cht->ht_xid);
  app_state_cht->ht_xid = Tbegin();
  return 1;
}


state_name commit_cht(void * dfaSet, StateMachine * stateMachine, Message * m, char * from) {
  
  setup_vars;
  
  assert(xid_exists);
  Tcommit(xid);
  //  jbHtRemove(ht_xid, xid_ht, (byte*)&(stateMachine->machine_id), sizeof(state_machine_id), (byte*)&xid);
  ThashRemove(app_state_cht->ht_xid, xid_ht, (byte*)&(stateMachine->machine_id), sizeof(state_machine_id));
  Tcommit(app_state_cht->ht_xid);
  app_state_cht->ht_xid = Tbegin();
    /* }*/
  /** @todo why was there an assert(0) in commit_cht?? */
  /** @todo On a commit, should responses of type AWAIT_RESULT ever be done by subordinates? */
//  if(m->response_type == AWAIT_RESULT) {
   // printf("commit_cht responding on an AWAIT_RESULT request.\n");
   // assert(0);
    /*    respond_once(&((DfaSet*)dfaSet)->networkSetup, SUBORDINATE_ACKING_2PC, m, __header_ptr(m)->initiator); */
//  }
  /* TODO: Check error codes, and return accordingly... */
  return 1;
}

state_name tally_cht(void * dfaSet, StateMachine * stateMachine, Message * m, char * from) {
  return 1;
}

DfaSet * cHtCoordinatorInit(char * configFile, short (*partition_function)(DfaSet *, Message *)) {
  NetworkSetup * config = readNetworkConfig(configFile, COORDINATOR);
 /* DfaSet * ret = cHtInit(CHT_COORDINATOR, config->localhost, partition_function, 
                       config->localport, config->broadcast_lists, config->broadcast_lists_count,
		       config->broadcast_list_host_count);
  free (config);*/
  DfaSet * ret = cHtInit(CHT_COORDINATOR, partition_function, config);
  free(config);
  return ret;
}

DfaSet * cHtSubordinateInit(char * configFile, short (*partition_function)(DfaSet *, Message *), int subordinate_number) {
  NetworkSetup * config = readNetworkConfig(configFile, subordinate_number);
/*  DfaSet * ret = cHtInit(CHT_SERVER, config->localhost, partition_function, 
                       config->localport, config->broadcast_lists, config->broadcast_lists_count,
		       config->broadcast_list_host_count);*/
  DfaSet * ret = cHtInit(CHT_SERVER, partition_function, config);
  free (config);
  return ret;
}
void debug_print_message(Message * m) {
  printf("debug: (key length %d) %d -> %d\n", getKeyLength(m), *(int*)getKeyAddr(m), *(int*)getValAddr(m));
  fflush(NULL);
}
