// $Id$
//
//  Copyright (C) 2002-2010 Greg Landrum and Rational Discovery LLC
//
//   @@ All Rights Reserved  @@
//

#include "FileParsers.h"
#include "MolFileStereochem.h"
#include <GraphMol/RDKitQueries.h>
#include <RDGeneral/StreamOps.h>
#include <RDGeneral/RDLog.h>

#include <fstream>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <RDGeneral/FileParseException.h>
#include <RDGeneral/BadFileException.h>
#include <typeinfo>

namespace RDKit{
  namespace {
    void completeQueryAndChildren(ATOM_EQUALS_QUERY *query,Atom *tgt,int magicVal){
      PRECONDITION(query,"no query");
      PRECONDITION(tgt,"no atom");
      if(query->getVal()==magicVal){
        int tgtVal=query->getDataFunc()(tgt);
        query->setVal(tgtVal);
      }
      QueryAtom::QUERYATOM_QUERY::CHILD_VECT_CI childIt;
      for(childIt=query->beginChildren();childIt!=query->endChildren();++childIt){
        completeQueryAndChildren((ATOM_EQUALS_QUERY *)(childIt->get()),tgt,magicVal);
      }
    }
    void CompleteMolQueries(RWMol *mol,int magicVal=-0xDEADBEEF){
      for (ROMol::AtomIterator ai=mol->beginAtoms();
           ai != mol->endAtoms(); ++ai){
        if((*ai)->hasQuery()){
          ATOM_EQUALS_QUERY *query=static_cast<ATOM_EQUALS_QUERY *>((*ai)->getQuery());
          completeQueryAndChildren(query,*ai,magicVal);
        }
      }
    }
    
    // it's kind of stinky that we have to do this, but as of g++3.2 and
    // boost 1.30, on linux calls to lexical_cast<int>(std::string)
    // crash if the string starts with spaces.
    template <typename T>
    T stripSpacesAndCast(const std::string &input,bool acceptSpaces=false){
      std::string trimmed=boost::trim_copy(input);
      if(acceptSpaces && trimmed==""){
	return 0;
      } else {
	return boost::lexical_cast<T>(trimmed);
      }
    }

    int toInt(const std::string &input,bool acceptSpaces=false){
      // atoi returns zero on failure:
      int res=atoi(input.c_str());
      if(!res && !acceptSpaces && input[0]==' '){
	std::string trimmed=boost::trim_copy(input);
	if(trimmed.length()==0) throw boost::bad_lexical_cast();
      }
      return res;
    }

    double toDouble(const std::string &input,bool acceptSpaces=true){
      // atof returns zero on failure:
      double res=atof(input.c_str());
      if(res==0.0 && !acceptSpaces && input[0]==' '){
	std::string trimmed=boost::trim_copy(input);
	if(trimmed.length()==0) throw boost::bad_lexical_cast();
      }
      return res;
    }

    Atom *replaceAtomWithQueryAtom(RWMol *mol,Atom *atom){
      PRECONDITION(mol,"bad molecule");
      PRECONDITION(atom,"bad atom");
      if(atom->hasQuery()) return atom;

      QueryAtom qa(*atom);
      unsigned int idx=atom->getIdx();

      if(atom->getFormalCharge()!=0){
	qa.expandQuery(makeAtomFormalChargeQuery(atom->getFormalCharge()));
      }
      if(atom->hasProp("_hasMassQuery")){
	qa.expandQuery(makeAtomMassQuery(static_cast<int>(atom->getMass())));
      }
      mol->replaceAtom(idx,&qa);
      return mol->getAtomWithIdx(idx);
    }

    //*************************************
    //
    // Every effort has been made to adhere to MDL's standard
    // for mol files
    //  
    //*************************************

    void ParseOldAtomList(RWMol *mol,const std::string &text){
      PRECONDITION(mol,"bad mol");
      unsigned int idx;
      try {
        idx = stripSpacesAndCast<unsigned int>(text.substr(0,3))-1;
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(0,3) << " to int";
        throw FileParseException(errout.str()) ;
      }

      RANGE_CHECK(0,idx,mol->getNumAtoms()-1);
      QueryAtom a(*(mol->getAtomWithIdx(idx)));

      ATOM_OR_QUERY *q = new ATOM_OR_QUERY;
      q->setDescription("AtomOr");
    
      switch(text[4]){
      case 'T':
        q->setNegation(true);
        break;
      case 'F':
        q->setNegation(false);
        break;
      default:
        std::ostringstream errout;
        errout << "Unrecognized atom-list query modifier: " << text[14];
        throw FileParseException(errout.str()) ;
      }          
    
      int nQueries;
      try {
        nQueries = toInt(text.substr(9,1));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(9,1) << " to int";
        throw FileParseException(errout.str()) ;
      }

      RANGE_CHECK(0,nQueries,5);
      for(int i=0;i<nQueries;i++){
        int pos = 11+i*4;
        int atNum;
        try {
          atNum = toInt(text.substr(pos,3));
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(pos,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
        RANGE_CHECK(0,atNum,200);  // goofy!
        q->addChild(QueryAtom::QUERYATOM_QUERY::CHILD_TYPE(makeAtomNumEqualsQuery(atNum)));
        if(!i) a.setAtomicNum(atNum);
      }
    
      a.setQuery(q);
      mol->replaceAtom(idx,&a); 
    };
  
    void ParseChargeLine(RWMol *mol, const std::string &text,bool firstCall) {
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,6)==std::string("M  CHG"),"bad charge line");
    

      // if this line is specified all the atom other than those specified
      // here should carry a charge of 0; but we should only do this once:
      if(firstCall){
        for (ROMol::AtomIterator ai = mol->beginAtoms();
             ai != mol->endAtoms(); ++ai) {
          (*ai)->setFormalCharge(0);
        }
      }

      int ie, nent;
      try {
        nent = toInt(text.substr(6,3));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(6,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
      int spos = 9;
      for (ie = 0; ie < nent; ie++) {
        int aid, chg;
        try {
          aid = toInt(text.substr(spos,4));
          spos += 4;
          chg = toInt(text.substr(spos,4));
          spos += 4;
          mol->getAtomWithIdx(aid-1)->setFormalCharge(chg);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(spos,4) << " to int";
          throw FileParseException(errout.str()) ;
        }
      }
    }

    void ParseRadicalLine(RWMol *mol, const std::string &text,bool firstCall) {
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,6)==std::string("M  RAD"),"bad charge line");

      // if this line is specified all the atom other than those specified
      // here should carry a charge of 0; but we should only do this once:
      if(firstCall){
        for (ROMol::AtomIterator ai = mol->beginAtoms();
             ai != mol->endAtoms(); ++ai) {
          (*ai)->setFormalCharge(0);
        }
      }

      int ie, nent;
      try {
        nent = toInt(text.substr(6,3));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(6,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
      int spos = 9;
      for (ie = 0; ie < nent; ie++) {
        int aid, rad;
        std::ostringstream errout;
      
        try {
          aid = toInt(text.substr(spos,4));
          spos += 4;
          rad = toInt(text.substr(spos,4));
          spos += 4;

          switch(rad) {
          case 1:
            mol->getAtomWithIdx(aid-1)->setNumRadicalElectrons(2);
            break;
          case 2:
            mol->getAtomWithIdx(aid-1)->setNumRadicalElectrons(1);
            break;
          case 3:
            mol->getAtomWithIdx(aid-1)->setNumRadicalElectrons(2);
            break;
          default:
            errout << "Unrecognized radical value " << rad << " for atom "<< aid-1 << std::endl;
            throw FileParseException(errout.str()) ;
          }
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(spos,4) << " to int";
          throw FileParseException(errout.str()) ;
        }
      }
    }

    void ParseIsotopeLine(RWMol *mol, const std::string &text){
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,6)==std::string("M  ISO"),"bad isotope line");
    
      unsigned int nent;
      try {
        nent = stripSpacesAndCast<unsigned int>(text.substr(6,3));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(6,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
      unsigned int spos = 9;
      for (unsigned int ie = 0; ie < nent; ie++) {
        unsigned int aid;
        int mass;
        try {
          aid = stripSpacesAndCast<unsigned int>(text.substr(spos,4));
          spos += 4;
          Atom *atom=mol->getAtomWithIdx(aid-1); 
          if(text.size()>=spos+4 && text.substr(spos,4)!="    "){
            mass = toInt(text.substr(spos,4));
            atom->setMass(static_cast<double>(mass));
            spos += 4;
          } else {
            atom->setMass(PeriodicTable::getTable()->getAtomicWeight(atom->getAtomicNum()));
          }
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(spos,4) << " to int";
          throw FileParseException(errout.str()) ;
        }
      }
    
    }

    void ParseSubstitutionCountLine(RWMol *mol, const std::string &text){
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,6)==std::string("M  SUB"),"bad SUB line");
    
      unsigned int nent;
      try {
        nent = stripSpacesAndCast<unsigned int>(text.substr(6,3));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(6,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
      unsigned int spos = 9;
      for (unsigned int ie = 0; ie < nent; ie++) {
        unsigned int aid;
        int count;
        try {
          aid = stripSpacesAndCast<unsigned int>(text.substr(spos,4));
          spos += 4;
          Atom *atom=mol->getAtomWithIdx(aid-1); 
          if(text.size()>=spos+4 && text.substr(spos,4)!="    "){
            count = toInt(text.substr(spos,4));
            if(count==0) continue;
            ATOM_EQUALS_QUERY *q=makeAtomExplicitDegreeQuery(0);
            switch(count){
            case -1:
              q->setVal(0);break;
            case -2:
              q->setVal(atom->getDegree());break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
              q->setVal(count);break;
            case 6:
              BOOST_LOG(rdWarningLog) << " atom degree query with value 6 found. This will not match degree >6. The MDL spec says it should.";
              q->setVal(6);break;
            default:
              std::ostringstream errout;
              errout << "Value " << count << " is not supported as a degree query.";
              throw FileParseException(errout.str()) ;
            }
            if(!atom->hasQuery()){
	      atom=replaceAtomWithQueryAtom(mol,atom);
            }
            atom->expandQuery(q,Queries::COMPOSITE_AND);
            spos += 4;
          }
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(spos,4) << " to int";
          throw FileParseException(errout.str()) ;
        }
      }
    }

    void ParseUnsaturationLine(RWMol *mol, const std::string &text){
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,6)==std::string("M  UNS"),"bad UNS line");
    
      unsigned int nent;
      try {
        nent = stripSpacesAndCast<unsigned int>(text.substr(6,3));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(6,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
      unsigned int spos = 9;
      for (unsigned int ie = 0; ie < nent; ie++) {
        unsigned int aid;
        int count;
        try {
          aid = stripSpacesAndCast<unsigned int>(text.substr(spos,4));
          spos += 4;
          Atom *atom=mol->getAtomWithIdx(aid-1); 
          if(text.size()>=spos+4 && text.substr(spos,4)!="    "){
            count = toInt(text.substr(spos,4));
            if(count==0){
              continue;
            } else if(count==1){
              ATOM_EQUALS_QUERY *q=makeAtomUnsaturatedQuery();
              if(!atom->hasQuery()){
		atom=replaceAtomWithQueryAtom(mol,atom);
              }
              atom->expandQuery(q,Queries::COMPOSITE_AND);
            } else {
              std::ostringstream errout;
              errout << "Value " << count << " is not supported as an unsaturation query (only 0 and 1 are allowed).";
              throw FileParseException(errout.str()) ;
            }
          }
        }catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(spos,4) << " to int";
          throw FileParseException(errout.str()) ;
        }

      }
    }

    void ParseRingBondCountLine(RWMol *mol, const std::string &text){
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,6)==std::string("M  RBC"),"bad RBC line");
    
      unsigned int nent;
      try {
        nent = stripSpacesAndCast<unsigned int>(text.substr(6,3));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(6,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
      unsigned int spos = 9;
      for (unsigned int ie = 0; ie < nent; ie++) {
        unsigned int aid;
        int count;
        try {
          aid = stripSpacesAndCast<unsigned int>(text.substr(spos,4));
          spos += 4;
          Atom *atom=mol->getAtomWithIdx(aid-1); 
          if(text.size()>=spos+4 && text.substr(spos,4)!="    "){
            count = toInt(text.substr(spos,4));
            if(count==0) continue;
            ATOM_EQUALS_QUERY *q=makeAtomRingBondCountQuery(0);
            switch(count){
            case -1:
              q->setVal(0);break;
            case -2:
              q->setVal(-0xDEADBEEF);
              mol->setProp("_NeedsQueryScan",1);
              break;
            case 1:
            case 2:
            case 3:
              q->setVal(count);break;
            case 4:
              delete q;
              q = static_cast<ATOM_EQUALS_QUERY *>(new ATOM_LESSEQUAL_QUERY);
              q->setVal(4);
              q->setDescription("AtomRingBondCount");
              q->setDataFunc(queryAtomRingBondCount);
              break;
            default:
              std::ostringstream errout;
              errout << "Value " << count << " is not supported as a ring-bond count query.";
              throw FileParseException(errout.str()) ;
            }
            if(!atom->hasQuery()){
	      atom=replaceAtomWithQueryAtom(mol,atom);
            }
            atom->expandQuery(q,Queries::COMPOSITE_AND);
            spos += 4;
          }
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(spos,4) << " to int";
          throw FileParseException(errout.str()) ;
        }
      }
    }

    void ParseNewAtomList(RWMol *mol,const std::string &text){
      if(text.size()<15){
        std::ostringstream errout;
        errout << "Atom list line too short: '"<<text<<"'";
        throw FileParseException(errout.str()) ;
      }
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,6)==std::string("M  ALS"),"bad atom list line");
    
      unsigned int idx;
      try {
        idx = stripSpacesAndCast<unsigned int>(text.substr(7,3))-1;
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(7,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
      RANGE_CHECK(0,idx,mol->getNumAtoms()-1);
      QueryAtom *a=0;
    
      int nQueries;
      try {
        nQueries = toInt(text.substr(10,3));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(10,3) << " to int";
        throw FileParseException(errout.str()) ;
      }

      ASSERT_INVARIANT(nQueries>0,"no queries provided");
      for(unsigned int i=0;i<static_cast<unsigned int>(nQueries);i++){
        unsigned int pos = 16+i*4;
        if(text.size()<pos+4){
          std::ostringstream errout;
          errout << "Atom list line too short: '"<<text<<"'";
          throw FileParseException(errout.str()) ;
        }

        std::string atSymb = text.substr(pos,4);
        atSymb.erase(atSymb.find(" "),atSymb.size());
        int atNum = PeriodicTable::getTable()->getAtomicNumber(atSymb);
        if(!i){
          a = new QueryAtom(*(mol->getAtomWithIdx(idx)));
          a->setAtomicNum(atNum);
        } else {
          a->expandQuery(makeAtomNumEqualsQuery(atNum),Queries::COMPOSITE_OR,true);
        }
      }
      ASSERT_INVARIANT(a,"no atom built");
      
      switch(text[14]){
      case 'T':
        a->getQuery()->setNegation(true);
        break;
      case 'F':
        a->getQuery()->setNegation(false);
        break;
      default:
        std::ostringstream errout;
        errout << "Unrecognized atom-list query modifier: " << text[14];
        throw FileParseException(errout.str()) ;
      }          

      mol->replaceAtom(idx,a); 
    };
  
    void ParseRGroupLabels(RWMol *mol,const std::string &text){
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,6)==std::string("M  RGP"),"bad R group label line");
    
      int nLabels;
      try {
        nLabels = toInt(text.substr(6,3));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(6,3) << " to int";
        throw FileParseException(errout.str()) ;
      }

      for(int i=0;i<nLabels;i++){
        int pos = 10+i*8;
        unsigned int atIdx;
        try {
          atIdx = stripSpacesAndCast<unsigned int>(text.substr(pos,3));
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(pos,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
        unsigned int rLabel;
        try {
          rLabel = stripSpacesAndCast<unsigned int>(text.substr(pos+4,3));
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(pos+4,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
        atIdx-=1;
        if(atIdx>mol->getNumAtoms()){
          std::ostringstream errout;
          errout << "Attempt to set R group label on nonexistent atom " << atIdx;
          throw FileParseException(errout.str()) ;
        }
        QueryAtom qatom(*(mol->getAtomWithIdx(atIdx)));
        qatom.setProp("_MolFileRLabel",rLabel);
        // the CTFile spec (June 2005 version) technically only allows
        // R labels up to 32. Since there are three digits, we'll accept
        // anything: so long as it's positive and less than 1000:
        if(rLabel>0 && rLabel<999){
          qatom.setMass(double(rLabel));
        }
        qatom.setQuery(makeAtomNullQuery());
        mol->replaceAtom(atIdx,&qatom); 
      }
    };
  
    void ParseAtomAlias(RWMol *mol,std::string text,const std::string &nextLine){
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,2)==std::string("A "),"bad atom alias line");
      
      unsigned int idx;
      try {
        idx = stripSpacesAndCast<unsigned int>(text.substr(3,3))-1;
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(3,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
      RANGE_CHECK(0,idx,mol->getNumAtoms()-1);
      Atom *at = mol->getAtomWithIdx(idx);
      at->setProp("molFileAlias",nextLine);
    };
  
    void ParseAtomValue(RWMol *mol,std::string text){
      PRECONDITION(mol,"bad mol");
      PRECONDITION(text.substr(0,2)==std::string("V "),"bad atom value line");
      
      unsigned int idx;
      try {
        idx = stripSpacesAndCast<unsigned int>(text.substr(3,3))-1;
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(3,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
      RANGE_CHECK(0,idx,mol->getNumAtoms()-1);
      Atom *at = mol->getAtomWithIdx(idx);
      at->setProp("molFileValue",text.substr(7,text.length()-7));
    };

    Atom *ParseMolFileAtomLine(const std::string text, RDGeom::Point3D &pos) {
      Atom *res = new Atom;
      std::string symb;
      int massDiff,chg,hCount;

      if(text.size()<34){
        std::ostringstream errout;
        errout << "Atom line too short: '"<<text<<"'";
        throw FileParseException(errout.str()) ;
      }

      try {
        pos.x = toDouble(text.substr(0,10));
        pos.y = toDouble(text.substr(10,10));
        pos.z = toDouble(text.substr(20,10));
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot process coordinates.";
        throw FileParseException(errout.str()) ;
      }
      symb = text.substr(31,3);
      symb = symb.substr(0,symb.find(' '));
    
      // REVIEW: should we handle missing fields at the end of the line?
      massDiff=0;
      if(text.size()>=36 && text.substr(34,2)!=" 0"){
        try {
          massDiff = toInt(text.substr(34,2),true);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(34,2) << " to int";
          throw FileParseException(errout.str()) ;
        }
      }    
      chg=0;
      if(text.size()>=39 && text.substr(36,3)!="  0"){
        try {
          chg = toInt(text.substr(36,3),true);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(36,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
      }
      hCount = 0;
      if(text.size()>=45 && text.substr(42,3)!="  0"){
        try {
          hCount = toInt(text.substr(42,3),true);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(42,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
      }
      if(symb=="L" || symb=="A" || symb=="Q" || symb=="*" || symb=="LP"
         || symb=="R" || symb=="R#" || (symb>="R0" && symb<="R9") ){
        if(symb=="A"||symb=="Q"||symb=="*"){
          QueryAtom *query=new QueryAtom(0);
          if(symb=="*"){
            // according to the MDL spec, these match anything
            query->setQuery(makeAtomNullQuery());
          } else if(symb=="Q"){
            ATOM_OR_QUERY *q = new ATOM_OR_QUERY;
            q->setDescription("AtomOr");
            q->setNegation(true);
            q->addChild(QueryAtom::QUERYATOM_QUERY::CHILD_TYPE(makeAtomNumEqualsQuery(6)));
            q->addChild(QueryAtom::QUERYATOM_QUERY::CHILD_TYPE(makeAtomNumEqualsQuery(1)));
            query->setQuery(q);
          } else if(symb=="A"){
            query->setQuery(makeAtomNumEqualsQuery(1));
            query->getQuery()->setNegation(true);
          }
          delete res;
          res=query;  
          // queries have no implicit Hs:
          res->setNoImplicit(true);
        } else {
          res->setAtomicNum(0);
        }
        if(massDiff==0&&symb[0]=='R'){
          if(symb=="R1") res->setMass(1);
          else if(symb=="R2") res->setMass(2);
          else if(symb=="R3") res->setMass(3);
          else if(symb=="R4") res->setMass(4);
          else if(symb=="R5") res->setMass(5);
          else if(symb=="R6") res->setMass(6);
          else if(symb=="R7") res->setMass(7);
          else if(symb=="R8") res->setMass(8);
          else if(symb=="R9") res->setMass(9);
        }
      } else if( symb=="D" ){  // mol blocks support "D" and "T" as shorthand... handle that.
        res->setAtomicNum(1); 
        res->setMass(2.014);
      } else if( symb=="T" ){  // mol blocks support "D" and "T" as shorthand... handle that.
        res->setAtomicNum(1);
        res->setMass(3.016);
      } else {
        res->setAtomicNum(PeriodicTable::getTable()->getAtomicNumber(symb));
        res->setMass(PeriodicTable::getTable()->getAtomicWeight(res->getAtomicNum()));
      }
    
      //res->setPos(pX,pY,pZ);
      if(chg!=0) res->setFormalCharge(4-chg);

      // FIX: this does not appear to be correct
      if(hCount==1){
        res->setNoImplicit(true);
      }
    
      if(massDiff!=0) {
        // FIX: this isn't precisely correct because we should be doing the difference w.r.t. most abundant species.
        res->setMass(res->getMass()+massDiff);
	res->setProp("_hasMassQuery",true);
      }
    
      if(text.size()>=42 && text.substr(39,3)!="  0"){
        int parity=0;
        try {
          parity = toInt(text.substr(39,3),true);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(39,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
        res->setProp("molParity",parity);
      }

      if(text.size()>=48 && text.substr(45,3)!="  0"){
        int stereoCare=0;
        try {
          stereoCare = toInt(text.substr(45,3),true);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(45,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
        res->setProp("molStereoCare",stereoCare);
      }
      if(text.size()>=51 && text.substr(48,3)!="  0"){
        int totValence=0;
        try {
          totValence= toInt(text.substr(48,3),true);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(48,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
        res->setProp("molTotValence",totValence);
      }
      if(text.size()>=63 && text.substr(60,3)!="  0"){
        int atomMapNumber=0;
        try {
          atomMapNumber = toInt(text.substr(60,3),true);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(60,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
        res->setProp("molAtomMapNumber",atomMapNumber);
      }
      if(text.size()>=66 && text.substr(63,3)!="  0"){
        int inversionFlag=0;
        try {
          inversionFlag= toInt(text.substr(63,3),true);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(63,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
        res->setProp("molInversionFlag",inversionFlag);
      }
      if(text.size()>=69 && text.substr(66,3)!="  0"){
        int exactChangeFlag=0;
        try {
          exactChangeFlag = toInt(text.substr(66,3),true);
        }
        catch (boost::bad_lexical_cast &) {
          std::ostringstream errout;
          errout << "Cannot convert " << text.substr(66,3) << " to int";
          throw FileParseException(errout.str()) ;
        }
        res->setProp("molExactChangeFlag",exactChangeFlag);
      }
      return res;
    };
  
    Bond *ParseMolFileBondLine(const std::string &text){
      int idx1,idx2,bType,stereo;
      int spos = 0;

      if(text.size()<9){
        std::ostringstream errout;
        errout << "Bond line too short: '"<<text<<"'";
        throw FileParseException(errout.str()) ;
      }

      try {
        idx1 = toInt(text.substr(spos,3));
        spos += 3;
        idx2 = toInt(text.substr(spos,3));
        spos += 3;
        bType = toInt(text.substr(spos,3));  
      }
      catch (boost::bad_lexical_cast &) {
        std::ostringstream errout;
        errout << "Cannot convert " << text.substr(spos,3) << " to int";
        throw FileParseException(errout.str()) ;
      }
    
      // adjust the numbering
      idx1--;idx2--;

      Bond::BondType type;
      Bond *res=0;  
      switch(bType){
      case 1: type = Bond::SINGLE;res = new Bond;break;
      case 2: type = Bond::DOUBLE;res = new Bond;break;
      case 3: type = Bond::TRIPLE;res = new Bond;break;
      case 4: type = Bond::AROMATIC;res = new Bond;break;
      case 0:
        type = Bond::UNSPECIFIED;
        res = new Bond;
        BOOST_LOG(rdWarningLog) << "bond with order 0 found. This is not part of the MDL specification."<<std::endl;
        break;
      default:
        type = Bond::UNSPECIFIED;
        // it's a query bond of some type
        res = new QueryBond;
        if(bType == 8){
          BOND_NULL_QUERY *q;
          q = makeBondNullQuery();
          res->setQuery(q);
        } else if (bType==5 || bType==6 || bType==7 ){
          BOND_OR_QUERY *q;
          q = new BOND_OR_QUERY;
          if(bType == 5){
            // single or double
            q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::SINGLE)));
            q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::DOUBLE)));
            q->setDescription("BondOr");
          } else if(bType == 6){
            // single or aromatic
            q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::SINGLE)));
            q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::AROMATIC)));      
            q->setDescription("BondOr");
          } else if(bType == 7){
            // double or aromatic
            q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::DOUBLE)));
            q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::AROMATIC)));
            q->setDescription("BondOr");
          }
          res->setQuery(q);
        } else {
          BOND_NULL_QUERY *q;
          q = makeBondNullQuery();
          res->setQuery(q);
          BOOST_LOG(rdWarningLog) << "unrecognized query bond type, " << bType <<", found. Using an \"any\" query."<<std::endl;          
        }
        break;
      }
      res->setBeginAtomIdx(idx1);
      res->setEndAtomIdx(idx2);
      res->setBondType(type);

      if( text.size() >= 12 && text.substr(9,3)!="  0"){
        try {
          stereo = toInt(text.substr(9,3));
          //res->setProp("stereo",stereo);
          switch(stereo){
          case 0:
            res->setBondDir(Bond::NONE);
            break;
          case 1:
            res->setBondDir(Bond::BEGINWEDGE);
            break;
          case 6:
            res->setBondDir(Bond::BEGINDASH);
            break;
          case 3: // "either" double bond
            res->setBondDir(Bond::EITHERDOUBLE);
	    res->setStereo(Bond::STEREOANY);
	    break;
          case 4: // "either" single bond
            res->setBondDir(Bond::UNKNOWN);
            break;
          }
        } catch (boost::bad_lexical_cast) {
          ;
        }
      }
      if( text.size() >= 18 && text.substr(15,3)!="  0"){
        try {
          int topology = toInt(text.substr(15,3));
          QueryBond *qBond=new QueryBond(*res);
          BOND_EQUALS_QUERY *q=makeBondIsInRingQuery();
          switch(topology){
          case 1:
            break;
          case 2:
            q->setNegation(true);
            break;
          default:
            std::ostringstream errout;
            errout << "Unrecognized bond topology specifier: " << topology;
            throw FileParseException(errout.str()) ;
          }
          qBond->expandQuery(q);          
          delete res;
          res = qBond;
        } catch (boost::bad_lexical_cast) {
          ;
        }
      }
      if( text.size() >= 21 && text.substr(18,3)!="  0"){
        try {
          int reactStatus = toInt(text.substr(18,3));
          res->setProp("molReactStatus",reactStatus);
        } catch (boost::bad_lexical_cast) {
          ;
        }
      }
      return res;
    };  

    void ParseMolBlockAtoms(std::istream *inStream,unsigned int &line,
                           unsigned int nAtoms,RWMol *mol,Conformer *conf){
      PRECONDITION(inStream,"bad stream");
      PRECONDITION(mol,"bad molecule");
      PRECONDITION(conf,"bad conformer");
      for(unsigned int i=0;i<nAtoms;++i){
        ++line;
        std::string tempStr = getLine(inStream);
        if(inStream->eof()){
          throw FileParseException("EOF hit while reading atoms");
        }
        RDGeom::Point3D pos;
        Atom *atom = ParseMolFileAtomLine(tempStr, pos);
        unsigned int aid = mol->addAtom(atom,false,true);
        conf->setAtomPos(aid, pos);
      }
    }

    // returns whether or not any sign of chirality was detected
    void ParseMolBlockBonds(std::istream *inStream,unsigned int &line,
			    unsigned int nBonds,RWMol *mol,bool &chiralityPossible){
      PRECONDITION(inStream,"bad stream");
      PRECONDITION(mol,"bad molecule");
      for(unsigned int i=0;i<nBonds;++i){
        ++line;
        std::string tempStr = getLine(inStream);
        if(inStream->eof()){
          throw FileParseException("EOF hit while reading bonds");
        }
        Bond *bond = ParseMolFileBondLine(tempStr);
        // if we got an aromatic bond set the flag on the bond and the connected atoms
        if (bond->getBondType() == Bond::AROMATIC) {
          bond->setIsAromatic(true);
          mol->getAtomWithIdx(bond->getBeginAtomIdx())->setIsAromatic(true);
          mol->getAtomWithIdx(bond->getEndAtomIdx())->setIsAromatic(true);
        }
        // if the bond might have chirality info associated with it, set a flag:
        if(bond->getBondDir() != Bond::NONE && bond->getBondDir() != Bond::UNKNOWN){
          chiralityPossible=true;
        }
        mol->addBond(bond,true);
      }
    }

    bool ParseMolBlockProperties(std::istream *inStream,unsigned int &line,
                                 RWMol *mol){
      PRECONDITION(inStream,"bad stream");
      PRECONDITION(mol,"bad molecule");
      // older mol files can have an atom list block here
      std::string tempStr = getLine(inStream);
      ++line;
      if( tempStr[0] != 'M' && tempStr[0] != 'A'
          && tempStr[0] != 'V' && tempStr[0] != 'G'){
        ParseOldAtomList(mol,tempStr);
      }

      bool fileComplete=false;
      bool firstChargeLine=true;
      std::string lineBeg=tempStr.substr(0,6);
      while(!inStream->eof() && lineBeg!="M  END" && tempStr.substr(0,4)!="$$$$"){
        if(tempStr[0]=='A'){
          line++;
          std::string nextLine = getLine(inStream);
          if(tempStr.substr(0,6)!="M  END"){
            ParseAtomAlias(mol,tempStr,nextLine);
          }
        } else if(tempStr[0]=='G'){
          BOOST_LOG(rdWarningLog)<<" deprecated group abbreviation ignored"<<std::endl;
        } else if(tempStr[0]=='V'){
          ParseAtomValue(mol,tempStr);
        } else if(lineBeg=="S  SKP") {
          // pass
        }

        else if(lineBeg=="M  ALS") ParseNewAtomList(mol,tempStr);
        else if(lineBeg=="M  ISO") ParseIsotopeLine(mol,tempStr);
        else if(lineBeg=="M  RGP") ParseRGroupLabels(mol,tempStr);
        else if(lineBeg=="M  RBC") ParseRingBondCountLine(mol,tempStr);
        else if(lineBeg=="M  SUB") ParseSubstitutionCountLine(mol,tempStr);
        else if(lineBeg=="M  UNS") ParseUnsaturationLine(mol,tempStr);
        else if(lineBeg=="M  CHG") {
          ParseChargeLine(mol, tempStr,firstChargeLine);
          firstChargeLine=false;
        }
        else if(lineBeg=="M  RAD") {
          ParseRadicalLine(mol, tempStr,firstChargeLine);
          firstChargeLine=false;
        }
        line++;
        tempStr = getLine(inStream);
        lineBeg=tempStr.substr(0,6);
      }
      if(tempStr[0]=='M'&&tempStr.substr(0,6)=="M  END"){
        fileComplete=true;
      }
      return fileComplete;
    }

    std::string getV3000Line(std::istream *inStream,unsigned int &line){
      PRECONDITION(inStream,"bad stream");
      std::string res,tempStr;

      ++line;
      tempStr = getLine(inStream);
      if(tempStr.size()<7 || tempStr.substr(0,7) != "M  V30 "){
        std::ostringstream errout;
        errout << "Line "<<line<<" does not start with 'M  V30 '"<<std::endl;
        throw FileParseException(errout.str()) ;
      }
      // FIX: do we need to handle trailing whitespace after a -?
      while(tempStr[tempStr.length()-1]=='-'){
        // continuation character, append what we read:
        res += tempStr.substr(7,tempStr.length()-8);
        // and then read another line: 
        ++line;
        tempStr = getLine(inStream);
        if(tempStr.size()<7 || tempStr.substr(0,7) != "M  V30 "){
          std::ostringstream errout;
          errout << "Line "<<line<<" does not start with 'M  V30 '"<<std::endl;
          throw FileParseException(errout.str()) ;
        }
      }
      res += tempStr.substr(7,tempStr.length()-7);
     
      return res;
    }

    Atom *ParseV3000AtomSymbol(std::string token,bool negate,unsigned int &line){
      Atom *res=0;

      if(token[0]=='['){
        // atom list:
        if(token[token.length()-1]!=']'){
          std::ostringstream errout;
          errout << "Bad atom token '"<<token<<"' on line: "<<line;
          throw FileParseException(errout.str()) ;
        }
        token = token.substr(1,token.size()-2);

        std::vector<std::string> splitToken;
        boost::split(splitToken,token,boost::is_any_of(","));

        for(std::vector<std::string>::const_iterator stIt=splitToken.begin();
            stIt!=splitToken.end();++stIt){
          std::string atSymb=boost::trim_copy(*stIt);
          if(atSymb=="") continue;
          int atNum = PeriodicTable::getTable()->getAtomicNumber(atSymb);
          if(!res){
            res = new QueryAtom(atNum);
          } else {
            res->expandQuery(makeAtomNumEqualsQuery(atNum),Queries::COMPOSITE_OR,true);
          }
        }
        res->getQuery()->setNegation(negate);
      } else {
        if(negate) {
          throw FileParseException("NOT tokens only supported for atom lists") ;
        }
        // it's a normal CTAB atom symbol:
        if(token=="R#" || token=="A" || token=="Q" || token=="*"){
          if(token=="A"||token=="Q"||token=="*"){
            res=new QueryAtom(0);
            if(token=="*"){
              // according to the MDL spec, these match anything
              res->setQuery(makeAtomNullQuery());
            } else if(token=="Q"){
              ATOM_OR_QUERY *q = new ATOM_OR_QUERY;
              q->setDescription("AtomOr");
              q->setNegation(true);
              q->addChild(QueryAtom::QUERYATOM_QUERY::CHILD_TYPE(makeAtomNumEqualsQuery(6)));
              q->addChild(QueryAtom::QUERYATOM_QUERY::CHILD_TYPE(makeAtomNumEqualsQuery(1)));
              res->setQuery(q);
            } else if(token=="A"){
              res->setQuery(makeAtomNumEqualsQuery(1));
              res->getQuery()->setNegation(true);
            }
            // queries have no implicit Hs:
            res->setNoImplicit(true);
          } else {
            res->setAtomicNum(0);
          }
        } else if( token=="D" ){  // mol blocks support "D" and "T" as shorthand... handle that.
          res = new Atom(1);
          res->setMass(2.014);
        } else if( token=="T" ){  // mol blocks support "D" and "T" as shorthand... handle that.
          res = new Atom(1);
          res->setMass(3.016);
        } else {
          res = new Atom(PeriodicTable::getTable()->getAtomicNumber(token));
          res->setMass(PeriodicTable::getTable()->getAtomicWeight(res->getAtomicNum()));
        }
      }
      
      POSTCONDITION(res,"no atom built");
      return res;
    }

    bool splitAssignToken(const std::string &token,std::string &prop,std::string &val){
      std::vector<std::string> splitToken;
      boost::split(splitToken,token,
                   boost::is_any_of("="));
      if(splitToken.size()!=2){
        return false;
      }
      prop = splitToken[0];
      boost::to_upper(prop);
      val = splitToken[1];
      return true;
    }

    template <class T>
    void ParseV3000AtomProps(RWMol *mol,Atom *& atom,
			     typename T::iterator &token,const T &tokens,
                             unsigned int &line){
      PRECONDITION(mol,"bad molecule");
      PRECONDITION(atom,"bad atom");
      std::ostringstream errout;
      while(token!=tokens.end()){
        std::string prop,val;
        if(!splitAssignToken(*token,prop,val)){
          errout << "Invalid atom property: " << *token << " for atom "<< atom->getIdx()+1 << std::endl;
          throw FileParseException(errout.str()) ;
        }

        if(prop=="CHG"){
          int charge=toInt(val);
          if(!atom->hasQuery()) {
            atom->setFormalCharge(charge);
          } else {
            atom->expandQuery(makeAtomFormalChargeQuery(charge));
          }
        } else if(prop=="RAD"){
          // FIX handle queries here
          switch( toInt(val) ){
          case 0: break;
          case 1:
            atom->setNumRadicalElectrons(2);break;
          case 2:
            atom->setNumRadicalElectrons(1);break;
          case 3:
            atom->setNumRadicalElectrons(2);break;
          default:
            errout << "Unrecognized RAD value " << val << " for atom "<< atom->getIdx()+1 << std::endl;
            throw FileParseException(errout.str()) ;
          }
        } else if(prop=="MASS"){
          double v=toDouble(val);
          if(v<=0){
            errout << "Bad value for MASS :" << val << " for atom "<< atom->getIdx()+1 << std::endl;
            throw FileParseException(errout.str()) ;
          } else {
	    if(!atom->hasQuery()) {
	      atom->setMass(v);
	    } else {
	      atom->expandQuery(makeAtomMassQuery(static_cast<int>(v)));
	    }
	  }
        } else if(prop=="CFG"){
          int cfg=toInt(val);
          switch(cfg){
          case 0: break;
          case 1:
          case 2:
          case 3:
            atom->setProp("molParity",cfg);
            break;
          default:
            errout << "Unrecognized CFG value : " << val << " for atom "<< atom->getIdx()+1 << std::endl;
            throw FileParseException(errout.str()) ;
          }
        } else if(prop=="HCOUNT"){
	  if(val!="0"){
	    int hcount=toInt(val);
	    if(!atom->hasQuery()) {
	      atom=replaceAtomWithQueryAtom(mol,atom);
	    }
	    if(hcount==-1) hcount=0;
	    atom->expandQuery(makeAtomHCountQuery(hcount));
	  }
        } else if(prop=="UNSAT"){
	  if(val=="1"){
	    if(!atom->hasQuery()) {
	      atom=replaceAtomWithQueryAtom(mol,atom);
	    } 
	    atom->expandQuery(makeAtomUnsaturatedQuery());
	  }
        } else if(prop=="RBCNT"){
	  if(val!="0"){
	    int rbcount=toInt(val);
	    if(!atom->hasQuery()) {
	      atom=replaceAtomWithQueryAtom(mol,atom);
	    }
	    if(rbcount==-1) rbcount=0;
	    atom->expandQuery(makeAtomRingBondCountQuery(rbcount));
	  }
        } else if(prop=="AAMAP"){
	  if(val!="0"){
	    int mapno=toInt(val);
	    atom->setProp("molAtomMapNumber",mapno);
	  }
        }

        ++token;
      }
    }
    void ParseV3000AtomBlock(std::istream *inStream,unsigned int &line,
                             unsigned int nAtoms,RWMol *mol, Conformer *conf){
      PRECONDITION(inStream,"bad stream");
      PRECONDITION(nAtoms>0,"bad atom count");
      PRECONDITION(mol,"bad molecule");
      PRECONDITION(conf,"bad conformer");
      std::string tempStr;
      std::vector<std::string> splitLine;

      tempStr = getV3000Line(inStream,line);
      if(tempStr.length()<10 || tempStr.substr(0,10) != "BEGIN ATOM"){
        throw FileParseException("BEGIN ATOM line not found") ;
      }
      for(unsigned int i=0;i<nAtoms;++i){

        tempStr = getV3000Line(inStream,line);
        std::string trimmed=boost::trim_copy(tempStr);
        boost::escaped_list_separator<char> els(""," \t","'\"");
        boost::tokenizer<boost::escaped_list_separator<char> > tokens(trimmed,els);
        boost::tokenizer<boost::escaped_list_separator<char> >::iterator token;
        token=tokens.begin();

        if(token==tokens.end()) {
          std::ostringstream errout;
          errout << "Bad atom line : '"<<tempStr<<"'";
          throw FileParseException(errout.str()) ;
        }
        unsigned int molIdx=atoi(token->c_str());
        bool negate=false;
        
        // start with the symbol:
        ++token;
        if(token==tokens.end()) {
          std::ostringstream errout;
          errout << "Bad atom line : '"<<tempStr<<"'";
          throw FileParseException(errout.str()) ;
        }
        if(*token=="NOT"){
          negate=true;
          ++token;
          if(token==tokens.end()) {
            std::ostringstream errout;
            errout << "Bad atom line : '"<<tempStr<<"'";
            throw FileParseException(errout.str()) ;
          }
        }
        Atom *atom=ParseV3000AtomSymbol(*token,negate,line);

        
        // now the position;
        RDGeom::Point3D pos;
        ++token;
        if(token==tokens.end()) {
          std::ostringstream errout;
          errout << "Bad atom line : '"<<tempStr<<"'";
          throw FileParseException(errout.str()) ;
        }
        pos.x = atof(token->c_str());
        ++token;
        if(token==tokens.end()) {
          std::ostringstream errout;
          errout << "Bad atom line : '"<<tempStr<<"'";
          throw FileParseException(errout.str()) ;
        }
        pos.y = atof(token->c_str());
        ++token;
        if(token==tokens.end()) {
          std::ostringstream errout;
          errout << "Bad atom line : '"<<tempStr<<"'";
          throw FileParseException(errout.str()) ;
        }
        pos.z = atof(token->c_str());

        // the map number:
        ++token;
        if(token==tokens.end()) {
          std::ostringstream errout;
          errout << "Bad atom line : '"<<tempStr<<"'";
          throw FileParseException(errout.str()) ;
        }
        int mapNum=atoi(token->c_str());
        atom->setProp("molAtomMapNumber",mapNum);

        ++token;
        
        unsigned int aid=mol->addAtom(atom,false,true);

        // additional properties this may change the atom,
	// so be careful with it:
        ParseV3000AtomProps(mol,atom,token,tokens,line);

        mol->setAtomBookmark(atom,molIdx);
        conf->setAtomPos(aid,pos);
      }
      tempStr = getV3000Line(inStream,line);
      if(tempStr.length()<8 || tempStr.substr(0,8) != "END ATOM"){
        throw FileParseException("END ATOM line not found") ;
      }

      if(mol->hasProp("_2DConf")){
        conf->set3D(false);
        mol->clearProp("_2DConf");
      } else if(mol->hasProp("_3DConf")){
        conf->set3D(true);
        mol->clearProp("_3DConf");
      }
    }
    void ParseV3000BondBlock(std::istream *inStream,unsigned int &line,
                             unsigned int nBonds,RWMol *mol,
			     bool &chiralityPossible){
      PRECONDITION(inStream,"bad stream");
      PRECONDITION(nBonds>0,"bad bond count");
      PRECONDITION(mol,"bad molecule");

      std::string tempStr;
      std::vector<std::string> splitLine;

      tempStr = getV3000Line(inStream,line);
      if(tempStr.length()<10 || tempStr.substr(0,10) != "BEGIN BOND"){
        throw FileParseException("BEGIN BOND line not found") ;
      }
      for(unsigned int i=0;i<nBonds;++i){
        tempStr = boost::trim_copy(getV3000Line(inStream,line));
        boost::split(splitLine,tempStr,
                     boost::is_any_of(" \t"),boost::token_compress_on);
        if(splitLine.size()<4){
          std::ostringstream errout;
          errout << "bond line : "<<line<<" is too short";
          throw FileParseException(errout.str()) ;
        }
        Bond *bond;
        unsigned int bondIdx=atoi(splitLine[0].c_str());
        unsigned int bType=atoi(splitLine[1].c_str());
        unsigned int a1Idx=atoi(splitLine[2].c_str());
        unsigned int a2Idx=atoi(splitLine[3].c_str());

        switch(bType){
        case 1: bond = new Bond(Bond::SINGLE);break;
        case 2: bond = new Bond(Bond::DOUBLE);break;
        case 3: bond = new Bond(Bond::TRIPLE);break;
        case 4: bond = new Bond(Bond::AROMATIC);bond->setIsAromatic(true);break;
        case 0:
          bond = new Bond(Bond::UNSPECIFIED);
          BOOST_LOG(rdWarningLog) << "bond with order 0 found. This is not part of the MDL specification."<<std::endl;
          break;
        default:
          // it's a query bond of some type
          bond = new QueryBond;
          if(bType == 8){
            BOND_NULL_QUERY *q;
            q = makeBondNullQuery();
            bond->setQuery(q);
          } else if (bType==5 || bType==6 || bType==7 ){
            BOND_OR_QUERY *q;
            q = new BOND_OR_QUERY;
            if(bType == 5){
              // single or double
              q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::SINGLE)));
              q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::DOUBLE)));
              q->setDescription("BondOr");
            } else if(bType == 6){
              // single or aromatic
              q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::SINGLE)));
              q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::AROMATIC)));      
              q->setDescription("BondOr");
            } else if(bType == 7){
              // double or aromatic
              q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::DOUBLE)));
              q->addChild(QueryBond::QUERYBOND_QUERY::CHILD_TYPE(makeBondOrderEqualsQuery(Bond::AROMATIC)));
              q->setDescription("BondOr");
            }
            bond->setQuery(q);
          } else {
            BOND_NULL_QUERY *q;
            q = makeBondNullQuery();
            bond->setQuery(q);
            BOOST_LOG(rdWarningLog) << "unrecognized query bond type, " << bType <<", found. Using an \"any\" query."<<std::endl;          
          }
          break;
        }

        // additional bond properties:
        unsigned int lPos=4;
        std::ostringstream errout;
        while(lPos<splitLine.size()){
          std::string prop,val;
          if(!splitAssignToken(splitLine[lPos],prop,val)){
            errout << "bad bond property '"<<splitLine[lPos]<<"' on line "<<line;
            throw FileParseException(errout.str()) ;
          }
          if(prop=="CFG"){
            unsigned int cfg=atoi(val.c_str());
            switch(cfg){
            case 0: break;
            case 1:
              bond->setBondDir(Bond::BEGINWEDGE);
	      chiralityPossible=true;
              break;
            case 2:
              if(bType==1) bond->setBondDir(Bond::UNKNOWN);
              else if(bType==2){
		bond->setBondDir(Bond::EITHERDOUBLE);
		bond->setStereo(Bond::STEREOANY);
	      }
              break;
            case 3:
              bond->setBondDir(Bond::BEGINDASH);
	      chiralityPossible=true;
              break;
            default:
              errout << "bad bond CFG "<<val<<"' on line "<<line;
              throw FileParseException(errout.str()) ;
            }
          } else if(prop=="TOPO"){
            if(val!="0"){
              if(!bond->hasQuery()){
                QueryBond *qBond=new QueryBond(*bond);
                delete bond;
                bond=qBond;
              }
              BOND_EQUALS_QUERY *q=makeBondIsInRingQuery();
              if(val=="1"){
                // nothing
              } else if(val=="2"){
                q->setNegation(true);
              } else {
                errout << "bad bond TOPO "<<val<<"' on line "<<line;
                throw FileParseException(errout.str()) ;
              }
              bond->expandQuery(q);          
            }
          } else if(prop=="RXCTR"){
            int reactStatus = toInt(val);
            bond->setProp("molReactStatus",reactStatus);
          } else if(prop=="STBOX"){
          }
          ++lPos;
        }

        bond->setBeginAtomIdx(mol->getAtomWithBookmark(a1Idx)->getIdx());
        bond->setEndAtomIdx(mol->getAtomWithBookmark(a2Idx)->getIdx());
        mol->addBond(bond,true);
        if(bond->getIsAromatic()){
          mol->getAtomWithIdx(bond->getBeginAtomIdx())->setIsAromatic(true);
          mol->getAtomWithIdx(bond->getEndAtomIdx())->setIsAromatic(true);
        }
        mol->setBondBookmark(bond,bondIdx);
      }
      tempStr = getV3000Line(inStream,line);
      if(tempStr.length()<8 || tempStr.substr(0,8) != "END BOND"){
        throw FileParseException("END BOND line not found") ;
      }
    }
    bool ParseV3000MolBlock(std::istream *inStream,unsigned int &line,
                            RWMol *mol, Conformer *&conf,
			    bool &chiralityPossible){
      PRECONDITION(inStream,"bad stream");
      PRECONDITION(mol,"bad molecule");

      std::string tempStr;
      std::vector<std::string> splitLine;

      tempStr = getV3000Line(inStream,line);
      if(tempStr.length()<10 || tempStr.substr(0,10) != "BEGIN CTAB"){
        throw FileParseException("BEGIN CTAB line not found") ;
      }
      
      tempStr = getV3000Line(inStream,line);
      if(tempStr.size()<8 || tempStr.substr(0,7)!="COUNTS "){
        std::ostringstream errout;
        errout << "Bad counts line : '"<<tempStr<<"'";
        throw FileParseException(errout.str()) ;
      }
      std::string trimmed=boost::trim_copy(tempStr.substr(7,tempStr.length()-7));
      boost::split(splitLine,trimmed,boost::is_any_of(" \t"),boost::token_compress_on);
      if(splitLine.size()<2){
        std::ostringstream errout;
        errout << "Bad counts line : '"<<tempStr<<"'";
        throw FileParseException(errout.str()) ;
      }

      unsigned int nAtoms=toInt(splitLine[0]);
      unsigned int nBonds=toInt(splitLine[1]);
      if(nAtoms<=0){
        throw FileParseException("molecule has no atoms");
      }
      conf = new Conformer(nAtoms);
      
      unsigned int nSgroups,n3DConstraints,chiralFlag;
      if(splitLine.size()>2) nSgroups = toInt(splitLine[2]);
      if(splitLine.size()>3) n3DConstraints = toInt(splitLine[3]);
      if(splitLine.size()>4) chiralFlag = toInt(splitLine[4]);

      ParseV3000AtomBlock(inStream,line,nAtoms,mol,conf);
      ParseV3000BondBlock(inStream,line,nBonds,mol,chiralityPossible);

      if(nSgroups){
        BOOST_LOG(rdWarningLog)<<"S group information in mol block igored"<<std::endl;
        tempStr = getV3000Line(inStream,line);
        if(tempStr.length()<12 || tempStr.substr(0,12) != "BEGIN SGROUP"){
          throw FileParseException("BEGIN SGROUP line not found") ;
        }
        while(1){
          tempStr = getV3000Line(inStream,line);
          if(tempStr.length()>=10 && tempStr.substr(0,10) != "END SGROUP"){
            break;
          }
        }
      }
      if(n3DConstraints){
        BOOST_LOG(rdWarningLog)<<"3d constraint information in mol block igored"<<std::endl;
        tempStr = getV3000Line(inStream,line);
        if(tempStr.length()<11 || tempStr.substr(0,11) != "BEGIN OBJ3D"){
          throw FileParseException("BEGIN OBJ3D line not found") ;
        }
        for(unsigned int i=0;i<n3DConstraints;++i) tempStr = getV3000Line(inStream,line);
        tempStr = getV3000Line(inStream,line);
        if(tempStr.length()<9 || tempStr.substr(0,9) != "END OBJ3D"){
          throw FileParseException("END OBJ3D line not found") ;
        }
      }
      
      tempStr = getV3000Line(inStream,line);
      // do link nodes:
      while(tempStr.length()>8 && tempStr.substr(0,8)=="LINKNODE"){
        tempStr = getV3000Line(inStream,line);
      }

      while(tempStr.length()>5 && tempStr.substr(0,5)=="BEGIN"){
        // skip blocks we don't know how to read
        BOOST_LOG(rdWarningLog)<<"skipping block: "<<tempStr<<std::endl;
        tempStr = getV3000Line(inStream,line);
        //BOOST_LOG(rdWarningLog)<<"    >"<<tempStr<<std::endl;
        
        while(tempStr.length()<3 || tempStr.substr(0,3)!="END"){
          tempStr = getV3000Line(inStream,line);
          //BOOST_LOG(rdWarningLog)<<"    >"<<tempStr<<std::endl;
        }
        tempStr = getV3000Line(inStream,line);
      }

      if(tempStr.length()<8 || tempStr.substr(0,8) != "END CTAB"){
        throw FileParseException("END CTAB line not found") ;
      }

      mol->addConformer(conf, true);
      conf=0;

      return true;
    }

  }  // end of local namespace 

  //------------------------------------------------
  //
  //  Read a molecule from a stream
  //
  //------------------------------------------------
  RWMol *MolDataStreamToMol(std::istream *inStream, unsigned int &line, bool sanitize,
                            bool removeHs){
    PRECONDITION(inStream,"no stream");
    std::string tempStr;
    bool fileComplete=false;
    bool chiralityPossible = false;

    // mol name
    line++;
    tempStr = getLine(inStream);
    if(inStream->eof()){
      return NULL;
    }
    RWMol *res = new RWMol();
    res->setProp("_Name", tempStr);

    // info
    line++;
    tempStr = getLine(inStream);
    res->setProp("_MolFileInfo", tempStr);
    if(tempStr.length()>=22){
      std::string dimLabel=tempStr.substr(20,2);
      if(dimLabel=="2d"||dimLabel=="2D"){
        res->setProp("_2DConf",1);
      } else if(dimLabel=="3d"||dimLabel=="3D"){
        res->setProp("_3DConf",1);
      }
    }
    // comments
    line++;
    tempStr = getLine(inStream);
    res->setProp("_MolFileComments", tempStr);
        
    int nAtoms=0,nBonds=0,nLists=0,chiralFlag=0,nsText=0,nRxnComponents=0;
    int nReactants=0,nProducts=0,nIntermediates=0;
    // counts line, this is where we really get started
    line++;
    tempStr = getLine(inStream);

    if(tempStr.size()<6){
      std::ostringstream errout;
      errout << "Counts line too short: '"<<tempStr<<"'";
      throw FileParseException(errout.str()) ;
    }

    unsigned int spos = 0;
    // this needs to go into a try block because if the lexical_cast throws an
    // exception we want to catch and delete mol before leaving this function
    try {
      nAtoms = toInt(tempStr.substr(spos,3));
      spos = 3;
      nBonds = toInt(tempStr.substr(spos,3));
      spos = 6;
    } catch (boost::bad_lexical_cast &) {
      if (res) delete res;
      std::ostringstream errout;
      errout << "Cannot convert " << tempStr.substr(spos,3) << " to int";
      throw FileParseException(errout.str()) ;
    }
    try {
      spos = 6;
      if(tempStr.size()>=9)
        nLists = toInt(tempStr.substr(spos,3));

      spos = 12;
      if(tempStr.size()>=spos+3)
        chiralFlag = toInt(tempStr.substr(spos,3));

      spos = 15;
      if(tempStr.size()>=spos+3)
        nsText = toInt(tempStr.substr(spos,3));

      spos = 18;
      if(tempStr.size()>=spos+3)
        nRxnComponents = toInt(tempStr.substr(spos,3));

      spos = 21;
      if(tempStr.size()>=spos+3)
        nReactants   = toInt(tempStr.substr(spos,3));

      spos = 24;
      if(tempStr.size()>=spos+3)
        nProducts   = toInt(tempStr.substr(spos,3));

      spos = 27;
      if(tempStr.size()>=spos+3)
        nIntermediates = toInt(tempStr.substr(spos,3));

    } catch (boost::bad_lexical_cast &) {
      // some SD files (such as some from NCI) lack all the extra information
      // on the header line, so ignore problems parsing there.
    }

    unsigned int ctabVersion=2000;
    if(tempStr.size()>35){
      if(tempStr.size()<39 || tempStr[34]!='V'){
        if(res) delete res;
        throw FileParseException("CTAB version string invalid");
      }
      if(tempStr.substr(34,5)=="V3000"){
        ctabVersion=3000;
        //if(res) delete res;
        //throw FileParseException("V3000 CTABs not supported");
      } else if(tempStr.substr(34,5)!="V2000"){
        if(res) delete res;
        std::ostringstream errout;
        errout << "Unsupported CTAB version: '"<< tempStr.substr(34,5) << "'";
        throw FileParseException(errout.str()) ;
      }
    }
    
    Conformer *conf=0;
    try {
      if(ctabVersion==2000){
        if(nAtoms<=0){
          throw FileParseException("molecule has no atoms");
        }
        conf = new Conformer(nAtoms);

        ParseMolBlockAtoms(inStream,line,nAtoms,res,conf);

        if(res->hasProp("_2DConf")){
          conf->set3D(false);
          res->clearProp("_2DConf");
        } else if(res->hasProp("_3DConf")){
          conf->set3D(true);
          res->clearProp("_3DConf");
        }
        res->addConformer(conf, true);
        conf=0;

        ParseMolBlockBonds(inStream,line,nBonds,res,chiralityPossible);
      
        fileComplete=ParseMolBlockProperties(inStream,line,res);
      } else {
        if(nAtoms!=0 || nBonds!=0){
          throw FileParseException("V3000 mol blocks should have 0s in the initial counts line.") ;
        }
        fileComplete=ParseV3000MolBlock(inStream,line,res,conf,chiralityPossible);
      }
    }
    catch (FileParseException &e) { // catch any exception because of lexical_casting etc
      // and throw them back after cleanup
      if(res) delete res;
      if(conf) delete conf;
      throw e;
    }

    if(!fileComplete){
      if(res) delete res;
      throw FileParseException("Problems encountered parsing Mol data, M  END ");
    }

    // calculate explicit valence on each atom:
    for(RWMol::AtomIterator atomIt=res->beginAtoms();
        atomIt!=res->endAtoms();
        ++atomIt) {
      (*atomIt)->calcExplicitValence(false);
    }

    if (res && sanitize ) {
      // update the chirality and stereo-chemistry and stuff:
      //
      // NOTE: we detect the stereochemistry before sanitizing/removing
      // hydrogens because the removal of H atoms may actually remove
      // the wedged bond from the molecule.  This wipes out the only
      // sign that chirality ever existed and makes us sad... so first
      // perceive chirality, then remove the Hs and sanitize.
      //
      // One exception to this (of course, there's always an exception):
      // DetectAtomStereoChemistry() needs to check the number of
      // implicit hydrogens on atoms to detect if things can be
      // chiral. However, if we ask for the number of implicit Hs before
      // we've called MolOps::cleanUp() on the molecule, we'll get
      // exceptions for common "weird" cases like a nitro group
      // mis-represented as -N(=O)=O.  *SO*... we need to call
      // cleanUp(), then detect the stereochemistry.
      // (this was Issue 148)
      //
      if(chiralityPossible){
        MolOps::cleanUp(*res);
        const Conformer &conf = res->getConformer();
        DetectAtomStereoChemistry(*res, &conf);
      }

      try {
        if(removeHs){
          ROMol *tmp=MolOps::removeHs(*res,false,false);
          delete res;
          res = static_cast<RWMol *>(tmp);
        } else {
          MolOps::sanitizeMol(*res);
        }

        // now that atom stereochem has been perceived, the wedging
        // information is no longer needed, so we clear
        // single bond dir flags:
        ClearSingleBondDirFlags(*res);
      
        // unlike DetectAtomStereoChemistry we call DetectBondStereoChemistry 
        // here after sanitization because we need the ring information:
        const Conformer &conf = res->getConformer();
        DetectBondStereoChemistry(*res, &conf);
      }
      catch (MolSanitizeException &se){
        if(res) delete res;
        throw se;
      }
      MolOps::assignStereochemistry(*res,true);
    }

    if(res->hasProp("_NeedsQueryScan")){
      res->clearProp("_NeedsQueryScan");
      CompleteMolQueries(res);
    }

    return res;
  };
  

  RWMol *MolDataStreamToMol(std::istream &inStream, unsigned int &line,
                            bool sanitize, bool removeHs){
    return MolDataStreamToMol(&inStream,line,sanitize,removeHs);
  };
  //------------------------------------------------
  //
  //  Read a molecule from a string
  //
  //------------------------------------------------
  RWMol *MolBlockToMol(const std::string &molBlock, bool sanitize, bool removeHs){
    std::istringstream inStream(molBlock);
    unsigned int line = 0;
    return MolDataStreamToMol(inStream, line, sanitize, removeHs);
  }    


  //------------------------------------------------
  //
  //  Read a molecule from a file
  //
  //------------------------------------------------
  RWMol *MolFileToMol(std::string fName, bool sanitize, bool removeHs){
    std::ifstream inStream(fName.c_str());
    if (!inStream || (inStream.bad()) ) {
      std::ostringstream errout;
      errout << "Bad input file " << fName;
      throw BadFileException(errout.str());
    }
    RWMol *res=NULL;
    if(!inStream.eof()){
      unsigned int line = 0;
      res=MolDataStreamToMol(inStream, line, sanitize, removeHs);
    }
    return res;
  }    
}

