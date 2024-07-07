#include "driver.hpp"
#include "parser.hpp"

// Generazione di un'istanza per ciascuna della classi LLVMContext,
// Module e IRBuilder. Nel caso di singolo modulo è sufficiente
LLVMContext *context = new LLVMContext;
Module *module = new Module("Kaleidoscope", *context);
IRBuilder<> *builder = new IRBuilder(*context);

Value *LogErrorV(const std::string Str) {
  std::cerr << Str << std::endl;
  return nullptr;
}

/* Il codice seguente sulle prime non è semplice da comprendere.
   Esso definisce una utility (funzione C++) con due parametri:
   1) la rappresentazione di una funzione llvm IR, e
   2) il nome per un registro SSA
   La chiamata di questa utility restituisce un'istruzione IR che alloca un double
   in memoria e ne memorizza il puntatore in un registro SSA cui viene attribuito
   il nome passato come secondo parametro. L'istruzione verrà scritta all'inizio
   dell'entry block della funzione passata come primo parametro.
   Si ricordi che le istruzioni sono generate da un builder. Per non
   interferire con il builder globale, la generazione viene dunque effettuata
   con un builder temporaneo TmpB
*/
static AllocaInst *CreateEntryBlockAlloca(Function *fun, StringRef VarName, Type* T = Type::getDoubleTy(*context)) {
  IRBuilder<> TmpB(&fun->getEntryBlock(), fun->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*context), nullptr, VarName);
}

// Implementazione del costruttore della classe driver
driver::driver(): trace_parsing(false), trace_scanning(false) {};

// Implementazione del metodo parse
int driver::parse (const std::string &f) {
  file = f;                    // File con il programma
  location.initialize(&file);  // Inizializzazione dell'oggetto location
  scan_begin();                // Inizio scanning (ovvero apertura del file programma)
  yy::parser parser(*this);    // Istanziazione del parser
  parser.set_debug_level(trace_parsing); // Livello di debug del parsed
  int res = parser.parse();    // Chiamata dell'entry point del parser
  scan_end();                  // Fine scanning (ovvero chiusura del file programma)
  return res;
}

// Implementazione del metodo codegen, che è una "semplice" chiamata del 
// metodo omonimo presente nel nodo root (il puntatore root è stato scritto dal parser)
void driver::codegen() {
  root->codegen(*this);
};

/************************* Sequence tree **************************/
SeqAST::SeqAST(RootAST* first, RootAST* continuation):
  first(first), continuation(continuation) {};


// mediante chiamate ricorsive viene generato il codice di first e 
// poi quello di continuation (con gli opportuni controlli di "esistenza")
Value *SeqAST::codegen(driver& drv) {
  if (first != nullptr) {
    Value *f = first->codegen(drv);
  } else {
    if (continuation == nullptr) return nullptr;
  }
  Value *c = continuation->codegen(drv);
  return nullptr;
};

/********************* Number Expression Tree *********************/
NumberExprAST::NumberExprAST(double Val): Val(Val) {};

lexval NumberExprAST::getLexVal() const {
  // Non utilizzata, Inserita per continuità con versione precedente
  lexval lval = Val;
  return lval;
};

// Non viene generata un istruzione; ma una costante LLVM IR
// che corrisponde al valore float memorizzato nel nodo

Value *NumberExprAST::codegen(driver& drv) {  
  return ConstantFP::get(*context, APFloat(Val));
};

/******************** Variable Expression Tree ********************/
VariableExprAST::VariableExprAST(const std::string &Name): Name(Name) {};

lexval VariableExprAST::getLexVal() const {
  lexval lval = Name;
  return lval;
};

// NamedValues è una tabella che ad ogni variabile (che, in Kaleidoscope1.0, 
// può essere solo un parametro di funzione) associa non un valore bensì
// la rappresentazione di una funzione che alloca memoria e restituisce in un
// registro SSA il puntatore alla memoria allocata. Generare il codice corrispondente
// ad una varibile equivale dunque a recuperare il tipo della variabile 
// allocata e il nome del registro e generare una corrispondente istruzione di load
// Negli argomenti della CreateLoad ritroviamo quindi: (1) il tipo allocato, (2) il registro
// SSA in cui è stato messo il puntatore alla memoria allocata (si ricordi che A è
// l'istruzione ma è anche il registro, vista la corrispodenza 1-1 fra le due nozioni), (3)
// il nome del registro in cui verrà trasferito il valore dalla memoria
Value *VariableExprAST::codegen(driver& drv) {
  AllocaInst *A = drv.NamedValues[Name];
  
  if (!A){
    //Se la variabile non è locale, si tenta di trovarla tra le globali
    GlobalVariable* Global = module->getGlobalVariable(Name);

    if(!Global){
      //se fallisce variabile non definita
      return LogErrorV("Variabile "+Name+" non definita (ne localmente ne globalmente)");
    }

    else{
      //Viene trovata in globale, si crea la load
      return builder->CreateLoad(Type::getDoubleTy(*context), Global, Name.c_str());
    }

  }

  return builder->CreateLoad(A->getAllocatedType(), A, Name.c_str());
}

/******************** Binary Expression Tree **********************/
BinaryExprAST::BinaryExprAST(char Ope, ExprAST* LHS, ExprAST* RHS):
  Ope(Ope), LHS(LHS), RHS(RHS) {};

// La generazione del codice in questo caso è di facile comprensione.
// Vengono ricorsivamente generati il codice per il primo e quello per il secondo
// operando. Con i valori memorizzati in altrettanti registri SSA si
// costruisce l'istruzione utilizzando l'opportuno operatore
Value *BinaryExprAST::codegen(driver& drv) {
  if (!LHS){
    //Caso in cui non sia definita un'operazione binaria. L'operazione è stata inserita nella seguente classe in quanto si tratta comunque di un'operazione tra "due operatori" di cui uno è "già specificato" (CreateFNeg crea una fsub tra un double pari a 0 e il Value*)
    switch(Ope){
      case '-':
        return builder->CreateFNeg(RHS->codegen(drv), "neg");
      default:
        return LogErrorV("Attenzione! operazione non definita correttamente (LHS mancante e non si è nella operazione di negazione!)");
    }

  }

  Value *L = LHS->codegen(drv);
  Value *R = RHS->codegen(drv);

  if (!L || !R) 
     return nullptr;
  switch (Ope) {
  case '+':
    return builder->CreateFAdd(L,R,"addres");
  case '-':
    return builder->CreateFSub(L,R,"subres");
  case '*':
    return builder->CreateFMul(L,R,"mulres");
  case '/':
    return builder->CreateFDiv(L,R,"addres");
  case '<':
    return builder->CreateFCmpULT(L,R,"lttest");
  case '=':
    return builder->CreateFCmpUEQ(L,R,"eqtest");
  default:  
    std::cout << Ope << std::endl;
    return LogErrorV("operatore binario non corretto");
  }
};

/********************* Call Expression Tree ***********************/
/* Call Expression Tree */
CallExprAST::CallExprAST(std::string Callee, std::vector<ExprAST*> Args):
  Callee(Callee),  Args(std::move(Args)) {};

lexval CallExprAST::getLexVal() const {
  lexval lval = Callee;
  return lval;
};

Value* CallExprAST::codegen(driver& drv) {
  // La generazione del codice corrispondente ad una chiamata di funzione
  // inizia cercando nel modulo corrente (l'unico, nel nostro caso) una funzione
  // il cui nome coincide con il nome memorizzato nel nodo dell'AST
  // Se la funzione non viene trovata (e dunque non è stata precedentemente definita)
  // viene generato un errore

  Function *CalleeF = module->getFunction(Callee);
  if (!CalleeF)
     return LogErrorV("Funzione "+Callee+" non definita");
  // Il secondo controllo è che la funzione recuperata abbia tanti parametri
  // quanti sono gi argomenti previsti nel nodo AST
  if (CalleeF->arg_size() != Args.size())
     return LogErrorV("Numero di argomenti non corretto");
  // Passato con successo anche il secondo controllo, viene predisposta
  // ricorsivamente la valutazione degli argomenti presenti nella chiamata 
  // (si ricordi che gli argomenti possono essere espressioni arbitarie)
  // I risultati delle valutazioni degli argomenti (registri SSA, come sempre)
  // vengono inseriti in un vettore, dove "se li aspetta" il metodo CreateCall
  // del builder, che viene chiamato subito dopo per la generazione dell'istruzione
  // IR di chiamata
  std::vector<Value *> ArgsV;
  for (auto arg : Args) {
     ArgsV.push_back(arg->codegen(drv));
     if (!ArgsV.back()){
        std::cout<<"Errore qui!";
        return nullptr;
        }
  }
  return builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

/************************* If Expression Tree *************************/
IfExprAST::IfExprAST(ExprAST* Cond, ExprAST* TrueExp, ExprAST* FalseExp):
   Cond(Cond), TrueExp(TrueExp), FalseExp(FalseExp) {};
   
Value* IfExprAST::codegen(driver& drv) {
    // Viene dapprima generato il codice per valutare la condizione, che
    // memorizza il risultato (di tipo i1, dunque booleano) nel registro SSA 
    // che viene "memorizzato" in CondV. 
    Value* CondV = Cond->codegen(drv);
    if (!CondV)
       return LogErrorV("Errore, non c'è la condizione per l'if!");
    
    // Ora bisogna generare l'istruzione di salto condizionato, ma prima
    // vanno creati i corrispondenti basic block nella funzione attuale
    // (ovvero la funzione di cui fa parte il corrente blocco di inserimento)
    Function *function = builder->GetInsertBlock()->getParent();
    BasicBlock *TrueBB =  BasicBlock::Create(*context, "trueexp", function);
    // Il blocco TrueBB viene inserito nella funzione dopo il blocco corrente
    BasicBlock *FalseBB = BasicBlock::Create(*context, "falseexp");
    BasicBlock *MergeBB = BasicBlock::Create(*context, "endcond");
    
    //  l'istruzione di salto condizionato
    builder->CreateCondBr(CondV, TrueBB, FalseBB);
    
    // "Posizioniamo" il builder all'inizio del blocco true, 
    // generiamo ricorsivamente il codice da eseguire in caso di
    // condizione vera, poi si genera il salto 
    // incondizionato al blocco merge
    builder->SetInsertPoint(TrueBB);
    Value *TrueV = TrueExp->codegen(drv);
    if (!TrueV)
       return LogErrorV("Impossibile generare il codice per la TrueExpr");
    builder->CreateBr(MergeBB);
    
    // Come già ricordato, la chiamata di codegen in TrueExp potrebbe aver inserito 
    // altri blocchi (nel caso in cui la parte trueexp sia a sua volta un condizionale).
    // Ne consegue che il blocco corrente potrebbe non coincidere più con TrueBB.
    // Il branch alla parte merge deve però essere effettuato dal blocco corrente,
    // che dunque va recuperato.
    TrueBB = builder->GetInsertBlock();
    function->insert(function->end(), FalseBB);
    
    // "Posizioniamo" il builder all'inizio del blocco false, 
    // generiamo ricorsivamente il codice da eseguire in caso di
    // condizione falsa poi si genera il salto 
    // incondizionato al blocco merge
    builder->SetInsertPoint(FalseBB);
    
    Value *FalseV = FalseExp->codegen(drv);  
    if (!FalseV)
       return nullptr;
    builder->CreateBr(MergeBB);
    
    // si recupera il blocco corrente 
    FalseBB = builder->GetInsertBlock();
    function->insert(function->end(), MergeBB);
    
    //Generazioen del codice in cui i
    //flussi si riassestano. Si imposta il builder
    builder->SetInsertPoint(MergeBB);
  
    // Il codice di riunione dei flussi è una "semplice" istruzione PHI: 
    //a seconda del blocco da cui arriva il flusso, TrueBB o FalseBB, il valore
    // del costrutto condizionale (si ricordi che si tratta di un "expression if")
    // deve essere copiato (in un nuovo registro SSA) da TrueV o da FalseV
    // La creazione di un'istruzione PHI avviene però in due passi, in quanto
    // il numero di "flussi entranti" non è fissato.
    // 1) Dapprima si crea il nodo PHI specificando quanti sono i possibili nodi sorgente
    // 2) Per ogni possibile nodo sorgente, viene poi inserita l'etichetta e il registro
    //    SSA da cui prelevare il valore 
    PHINode *PN = builder->CreatePHI(Type::getDoubleTy(*context), 2, "condval");
    PN->addIncoming(TrueV, TrueBB);
    PN->addIncoming(FalseV, FalseBB);
    return PN;
};

/********************** Block Expression Tree *********************/
BlockExprAST::BlockExprAST(std::vector<VarBindingAST*> Def, ExprAST* Val): 
         Def(std::move(Def)), Val(Val) {};

Value* BlockExprAST::codegen(driver& drv) {
   // Un blocco è un'espressione preceduta dalla definizione di una o più variabili locali.
   // Le definizioni sono opzionali e tuttavia necessarie perché l'uso di un blocco
   // abbia senso. Ad ogni variabile deve essere associato il valore di una costante o il valore di
   // un'espressione. Nell'espressione, arbitraria, possono chiaramente comparire simboli di
   // variabile. Al riguardo, la gestione dello scope (ovvero delle regole di visibilità)
   // è implementata nel modo seguente, in cui, come esempio, consideremo la definzione: var y = x+1
   // 1) Viene dapprima generato il codice per valutare l'espressione x+1.
   //    L'area di memoria da cui "prelevare" il valore di x è scritta in un
   //    registro SSA che è parte della (rappresentazione interna della) istruzione alloca usata
   //    per allocare la memoria corrispondente e che è registrata nella symbol table
   //    Per i parametri della funzione, l'istruzione di allocazione viene generata (come già sappiamo)
   //    dalla chiamata di codegen in FunctionAST. Per le variabili locali viene generata nel presente
   //    contesto. Si noti, di passaggio, che tutte le istruzioni di allocazione verranno poi emesse
   //    nell'entry block, in ordine cronologico rovesciato (rispetto alla generazione). Questo perché
   //    la routine di utilità (CreateEntryBlockAlloca) genera sempre all'inizio del blocco.
   // 2) Ritornando all'esempio, bisogna ora gestire l'assegnamento ad y gestendone la visibilità. 
   //    Come prima cosa viene generata l'istruzione alloca per y. 
   //    Questa deve essere inserita nella symbol table per futuri riferimenti ad y
   //    all'interno del blocco. Tuttavia, se un'istruzione alloca per y fosse già presente nella symbol
   //    table (nel caso y sia un parametro) bisognerebbe "rimuoverla" temporaneamente e re-inserirla
   //    all'uscita del blocco. Questo è ciò che viene fatto dal presente codice, che utilizza
   //    al riguardo il vettore di appoggio "AllocaTmp" (che naturalmente è un vettore di
   //    di (puntatori ad) istruzioni di allocazione
   std::vector<AllocaInst*> AllocaTmp;
   for (int i=0, e=Def.size(); i<e; i++) {
      // Per ogni definizione di variabile si genera il corrispondente codice che
      // (in questo caso) non restituisce un registro SSA ma l'istruzione di allocazione
      AllocaInst *boundval = Def[i]->codegen(drv);
      if (!boundval)
         return LogErrorV("Errore in BLockExpr1");
      // Viene temporaneamente rimossa la precedente istruzione di allocazione
      // della stessa variabile (nome) e inserita quella corrente
      AllocaTmp.push_back(drv.NamedValues[Def[i]->getName()]);
      drv.NamedValues[Def[i]->getName()] = boundval;
   };

   // Ora (ed è la parte più "facile" da capire) viene generato il codice che
   // valuta l'espressione. Eventuali riferimenti a variabili vengono risolti
   // nella symbol table appena modificata
   Value *blockvalue = Val->codegen(drv);
      if (!blockvalue)
         return LogErrorV("Errore in BlockExpr");
   // Prima di uscire dal blocco, si ripristina lo scope esterno al costrutto
   for (int i=0, e=Def.size(); i<e; i++) {
        drv.NamedValues[Def[i]->getName()] = AllocaTmp[i];
   };

   // Il valore del costrutto/espressione var è ovviamente il valore (il registro SSA)
   // restituito dal codice di valutazione dell'espressione
   return blockvalue;
};

/************************* Var binding Tree *************************/
VarBindingAST::VarBindingAST(const std::string Name, ExprAST* Val): Name(Name), Val(Val) {};

VarBindingAST::VarBindingAST(const std::string Name, double max, std::vector<ExprAST*> Val): Name(Name), Max(max), ArrVal(Val) {};
   
const std::string& VarBindingAST::getName() const { 
   return Name; 
};

AllocaInst* VarBindingAST::codegen(driver& drv) {   
   // Viene subito recuperato il riferimento alla funzione in cui si trova
   // il blocco corrente. Il riferimento è necessario perché lo spazio necessario
   // per memorizzare una variabile (ovunque essa sia definita, si tratti cioè
   // di un parametro oppure di una variabile locale ad un blocco espressione)
   // viene sempre riservato nell'entry block della funzione. Ricordiamo che
   // l'allocazione viene fatta tramite l'utility CreateEntryBlockAlloca
   Function *fun = builder->GetInsertBlock()->getParent();

   // Ora viene generato il codice che definisce il valore della variabile
   Value *BoundVal = Val->codegen(drv);
   if (!BoundVal)  // Qualcosa è andato storto nella generazione del codice?
      return nullptr;
   // Se tutto ok, si genera l'struzione che alloca memoria per la varibile ...
   AllocaInst *Alloca = CreateEntryBlockAlloca(fun, Name);
   // ... e si genera l'istruzione per memorizzarvi il valore dell'espressione,
   // ovvero il contenuto del registro BoundVal
   builder->CreateStore(BoundVal, Alloca);
   
   // L'istruzione di allocazione (che include il registro "puntatore" all'area di memoria
   // allocata) viene restituita per essere inserita nella symbol table
   return Alloca;
};

/************************* Prototype Tree *************************/
PrototypeAST::PrototypeAST(std::string Name, std::vector<std::string> Args):
  Name(Name), Args(std::move(Args)), emitcode(true) {};  //Di regola il codice viene emesso

lexval PrototypeAST::getLexVal() const {
   lexval lval = Name;
   return lval;	
};

const std::vector<std::string>& PrototypeAST::getArgs() const { 
   return Args;
};

// Previene la doppia emissione del codice. Si veda il commento più avanti.
void PrototypeAST::noemit() { 
   emitcode = false; 
};

Function *PrototypeAST::codegen(driver& drv) {
  // Costruisce una struttura, qui chiamata FT, che rappresenta il "tipo" di una
  // funzione. Con ciò si intende a sua volta una coppia composta dal tipo
  // del risultato (valore di ritorno) e da un vettore che contiene il tipo di tutti
  // i parametri. Si ricordi, tuttavia, che nel nostro caso l'unico tipo è double.
  
  // Prima definiamo il vettore (qui chiamato Doubles) con il tipo degli argomenti
  std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*context));
  // Quindi definiamo il tipo (FT) della funzione
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(*context), Doubles, false);
  // Infine definiamo una funzione (al momento senza body) del tipo creato e con il nome
  // presente nel nodo AST. ExternalLinkage vuol dire che la funzione può avere
  // visibilità anche al di fuori del modulo
  Function *F = Function::Create(FT, Function::ExternalLinkage, Name, *module);

  // Ad ogni parametro della funzione F (che, è bene ricordare, è la rappresentazione 
  // llvm di una funzione, non è una funzione C++) attribuiamo ora il nome specificato dal
  // programmatore e presente nel nodo AST relativo al prototipo
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  /* Abbiamo completato la creazione del codice del prototipo.
     Il codice può quindi essere emesso, ma solo se esso corrisponde
     ad una dichiarazione extern. Se invece il prototipo fa parte
     della definizione "completa" di una funzione (prototipo+body) allora
     l'emissione viene fatta al momendo dell'emissione della funzione.
     In caso contrario nel codice si avrebbe sia una dichiarazione
     (come nel caso di funzione esterna) sia una definizione della stessa
     funzione.
  */
  if (emitcode) {
    F->print(errs());
    fprintf(stderr, "\n");
  };
  
  return F;
}

/************************* Function Tree **************************/
FunctionAST::FunctionAST(PrototypeAST* Proto, ExprAST* Body): Proto(Proto), Body(Body) {};

Function* FunctionAST::codegen(driver& drv) {
  // Verifica che la funzione non sia già presente nel modulo, cioò che non
  // si tenti una "doppia definizion"
  Function *function = 
      module->getFunction(std::get<std::string>(Proto->getLexVal()));
  // Se la funzione non è già presente, si prova a definirla, innanzitutto
  // generando (ma non emettendo) il codice del prototipo
  if (!function){
    function = Proto->codegen(drv);
    }
  else
    return nullptr;
  // Se, per qualche ragione, la definizione "fallisce" si restituisce nullptr
  if (!function)
    return nullptr;  

  // Altrimenti si crea un blocco di base in cui iniziare a inserire il codice
  BasicBlock *BB = BasicBlock::Create(*context, "entry", function);
  builder->SetInsertPoint(BB);
 
  // Ora viene la parte "più delicata". Per ogni parametro formale della
  // funzione, nella symbol table si registra una coppia in cui la chiave
  // è il nome del parametro mentre il valore è un'istruzione alloca, generata
  // invocando l'utility CreateEntryBlockAlloca già commentata.
  // Vale comunque la pena ricordare: l'istruzione di allocazione riserva 
  // spazio in memoria (nel nostro caso per un double) e scrive l'indirizzo
  // in un registro SSA
  // Il builder crea poi un'istruzione che memorizza il valore del parametro x
  // (al momento contenuto nel registro SSA %x) nell'area di memoria allocata.
  // Si noti che il builder conosce il registro che contiene il puntatore all'area
  // perché esso è parte della rappresentazione C++ dell'istruzione di allocazione
  // (variabile Alloca) 
  
  for (auto &Arg : function->args()) {
    // Genera l'istruzione di allocazione per il parametro corrente
    AllocaInst *Alloca = CreateEntryBlockAlloca(function, Arg.getName());
    // Genera un'istruzione per la memorizzazione del parametro nell'area
    // di memoria allocata
    builder->CreateStore(&Arg, Alloca);
    // Registra gli argomenti nella symbol table per eventuale riferimento futuro
    drv.NamedValues[std::string(Arg.getName())] = Alloca;
  } 
  // Ora può essere generato il codice corssipondente al body (che potrà
  // fare riferimento alla symbol table)

  if (Value *RetVal = Body->codegen(drv)) {
    // Se la generazione termina senza errori, ciò che rimane da fare è
    // di generare l'istruzione return, che ("a tempo di esecuzione") prenderà
    // il valore lasciato nel registro RetVal 
    builder->CreateRet(RetVal);

    // Effettua la validazione del codice e un controllo di consistenza
    verifyFunction(*function);
 
    // Emissione del codice su su stderr) 
    function->print(errs());
    fprintf(stderr, "\n");

    return function;
  }
  // Errore nella definizione. La funzione viene rimossa
  function->eraseFromParent();
  return nullptr;
};

/******************** Var Global AST ********************/

//Classe per la definizione di variabili globali
VarGlobalAST::VarGlobalAST(const std::string &Name): Name(Name) {};

const std::string& VarGlobalAST::getName() const { 
   return Name; 
};

Value* VarGlobalAST::codegen(driver& drv) {
  Type* doubleType = Type::getDoubleTy(*context);

  //Viene creata una nuova istanza della classe GlobalVariable built-in llvm.
  GlobalVariable *globalVar = new GlobalVariable(*module, doubleType, false, GlobalValue::CommonLinkage, Constant::getNullValue(doubleType), Name);

   globalVar->print(errs());
   fprintf(stderr, "\n");

   return nullptr;
};

/******************** StmtAST ********************/
StmtAST::StmtAST(ExprAST* expression, ExprAST* statement) 
  : Left(expression), Right(statement) {};

Value* StmtAST::codegen(driver& drv) {
  Value* begin = Left->codegen(drv);
  
  if (!begin) {return LogErrorV("Errore nel begin"); }

  if(Right){
    // Se è presente una parte destra bisogna ritornare il valore di ritorno della parte destra 
    Value* continuation = Right->codegen(drv);
    if(!continuation){ return LogErrorV("Errore nel continuation"); }
    return continuation;
  }
  
  //Nel momento in cui ho solo stmt nella produzione, allora sono arrivato alla istruzione di ritorno; ritorno il valore dato dalla espressione
  return begin;
};

/************************* AssignmentAST *************************/
AssignmentAST::AssignmentAST(std::string Name, ExprAST* Val = nullptr):
   Name(Name), Val(Val) {};

//Costruttore per gestire l'operatore '++'. Se l'espressione che si vuole valutare è ++i, allora viene invocato questo costruttore con passato come parametro '+'
AssignmentAST::AssignmentAST(std::string Name, char op):
   Name(Name), Op(op) {};

const std::string& AssignmentAST::getName() const { 
   return Name; 
};

Value* AssignmentAST::codegen(driver& drv) {
   
  //Gestione dell'operatore '++'
  if (Op == '+'){
    Function* fun = builder->GetInsertBlock()->getParent();

    //Ovviamente la variabile deve essere definita dentro il contesto locale altrimenti non sarebbe possibile effettuare il for su una variabile non definita
    Value* val = drv.NamedValues[Name];
    if (!val) return LogErrorV("Variabile non definita");
    
    //Viene generata una nuova istruzione su un registro SSA per effettuare la somma del valore, poi memorizzato con una store
    Value* Tmp = builder->CreateLoad(Type::getDoubleTy(*context), val, Name);

    //somma
    Value* One = builder->CreateFAdd(Tmp, ConstantFP::get(*context, APFloat(1.0)), "inc");

    Value* Store = builder->CreateStore(One, val);
    
    return val;
  }

  //Se l'operatore non è '+', allora viene eseguita l'operazione di assegnazione
  Value *BoundVal = Val->codegen(drv);
  if (!BoundVal)
    return LogErrorV("Errore nel Val di AssignmentAST");

   Value* val = drv.NamedValues[Name];

  if (!val){
    GlobalVariable* Global = module->getGlobalVariable(Name);

    if(!Global) return LogErrorV("Variabile non definita!");

    builder->CreateStore(BoundVal, Global);
    return Global;
  }
   builder->CreateStore(BoundVal, val);
   return val;
};

/************************* For Expression Tree *************************/

//La scelta di usare un RootAST come init è dovuta la fatto che bindin -> VarBindingAST() : RootAST
ForExprAST::ForExprAST(std::variant<VarBindingAST*, AssignmentAST*> start, ExprAST* cond, ExprAST* step, ExprAST* body):
   Start(start), Cond(cond), Step(step), Body(body) {};
   
Value* ForExprAST::codegen(driver& drv) {
  
  //La variabile start può essere o un VarBinding o un Assignment in quanto dipende se la variabile è già stata definita nel contesto o meno. Se già stata definita, 
  //recupero il tipo AssignmentAST dal variant start, viceversa recupero il tipo VarBindingAST. 
  //Successivamente richiamo il metodo getName() per ottenere il nome della variabile 
  std::string VarName;
  if (std::holds_alternative<VarBindingAST*>(Start))
    VarName = std::get<VarBindingAST*>(Start)->getName();
  else if (std::holds_alternative<AssignmentAST*>(Start))
    VarName = std::get<AssignmentAST*>(Start)->getName();
  else return nullptr;

  AllocaInst* AllocaTmp;

  //Se viene trovata la variabile, viene memorizzata
  if (drv.NamedValues[VarName] != nullptr){
    AllocaTmp = drv.NamedValues[VarName];
  }

  //altrimenti viene generata
  else {
    if (std::holds_alternative<VarBindingAST*>(Start))
      drv.NamedValues[VarName] = std::get<VarBindingAST*>(Start)->codegen(drv);
    else if (std::holds_alternative<AssignmentAST*>(Start))
      drv.NamedValues[VarName] = std::get<VarBindingAST*>(Start)->codegen(drv);
  }

  Function* function = builder->GetInsertBlock()->getParent();
  AllocaInst *Alloca = CreateEntryBlockAlloca(function, VarName);

  //Seguono una serie di istruzioni simili per l'if
  BasicBlock* CondBB = BasicBlock::Create(*context, "cond", function);
  BasicBlock* LoopBB = BasicBlock::Create(*context, "loop");
  BasicBlock* MergeBB = BasicBlock::Create(*context, "merge");

  builder->CreateBr(CondBB);

  builder->SetInsertPoint(CondBB);

  //Blocco condizione
  Value* CondV = Cond->codegen(drv);
    if (!CondV) return nullptr;
  
  CondBB = builder->GetInsertBlock();
  function->insert(function->end(), LoopBB);
  
  //Codice per il salto condizionato
  builder->CreateCondBr(CondV, LoopBB, MergeBB);

  //Blocco Loop
  builder->SetInsertPoint(LoopBB);
  Value* BodyV = Body->codegen(drv);
    if (!BodyV) return nullptr;
  Value* StepV = Step->codegen(drv);
    if (!StepV) return nullptr;

  LoopBB = builder->GetInsertBlock();
  builder->CreateBr(CondBB);

  function->insert(function->end(), MergeBB);

  //insert del blocco Merge
  builder->SetInsertPoint(MergeBB);

  //Se è stato usato un AllocaTmp, viene inserito il valore dentro la variabile VarName, in quanto deve essere aggiornato nel caso la variabile non sia stata definita "in loco", ma fosse già presente in memoria.
  if (AllocaTmp){
    drv.NamedValues[VarName] = AllocaTmp;
  }

  return function;

};

/******************** CreateCondExp **********************/
CondExprAST::CondExprAST(char Op, ExprAST* LHS, ExprAST* RHS):
  Op(Op), LHS(LHS), RHS(RHS) {};

CondExprAST::CondExprAST(char Op, ExprAST* RHS):
  Op(Op), RHS(RHS) {};


Value *CondExprAST::codegen(driver& drv) {

  //Caso specifico dell'operatore NOT, che non presenta una parte sinistra 
  if (!LHS && Op == '!')
    return builder->CreateNot(RHS->codegen(drv),"not");

  Value *L = LHS->codegen(drv);
  Value *R = RHS->codegen(drv);

  if (!L || !R) 
     return nullptr;
  switch (Op) {
  case '&':
    return builder->CreateAnd(L,R,"and");
  case '|':
    return builder->CreateOr(L,R,"or");
  default:  
    std::cout << Op << std::endl;
    return LogErrorV("Operatore di condizione non definito!");
  }
};