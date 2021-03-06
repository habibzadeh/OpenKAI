#include "APcopter_follow.h"

namespace kai
{

APcopter_follow::APcopter_follow()
{
	m_pAP = NULL;
	m_pPC = NULL;
	m_pDet = NULL;
	m_tStampDet = 0;
	m_iClass = -1;

	m_iTracker = 0;
	m_bUseTracker = false;
	m_pTracker[0] = NULL;
	m_pTracker[1] = NULL;
	m_pTnow = NULL;
	m_pTnew = NULL;

	m_vTarget.init();

	m_vGimbal.init();
	m_mountControl.input_a = m_vGimbal.x * 100;	//pitch
	m_mountControl.input_b = m_vGimbal.y * 100;	//roll
	m_mountControl.input_c = m_vGimbal.z * 100;	//yaw
	m_mountControl.save_position = 0;

	m_mountConfig.stab_pitch = 0;
	m_mountConfig.stab_roll = 0;
	m_mountConfig.stab_yaw = 0;
	m_mountConfig.mount_mode = 2;
}

APcopter_follow::~APcopter_follow()
{
}

bool APcopter_follow::init(void* pKiss)
{
	IF_F(!this->ActionBase::init(pKiss));
	Kiss* pK = (Kiss*) pKiss;

	KISSm(pK,iClass);
	KISSm(pK,bUseTracker);

	Kiss* pG = pK->o("gimbal");
	if(!pG->empty())
	{
		pG->v("pitch", &m_vGimbal.x);
		pG->v("roll", &m_vGimbal.y);
		pG->v("yaw", &m_vGimbal.z);

		m_mountControl.input_a = m_vGimbal.x * 100;	//pitch
		m_mountControl.input_b = m_vGimbal.y * 100;	//roll
		m_mountControl.input_c = m_vGimbal.z * 100;	//yaw
		m_mountControl.save_position = 0;

		pG->v("stabPitch", &m_mountConfig.stab_pitch);
		pG->v("stabRoll", &m_mountConfig.stab_roll);
		pG->v("stabYaw", &m_mountConfig.stab_yaw);
		pG->v("mountMode", &m_mountConfig.mount_mode);
	}

	//link
	string iName;

	if(m_bUseTracker)
	{
		iName = "";
		pK->v("_TrackerBase1", &iName);
		m_pTracker[0] = (_TrackerBase*) (pK->root()->getChildInst(iName));
		IF_Fl(!m_pTracker[0], iName + ": not found");

		iName = "";
		pK->v("_TrackerBase2", &iName);
		m_pTracker[1] = (_TrackerBase*) (pK->root()->getChildInst(iName));
		IF_Fl(!m_pTracker[1], iName + ": not found");

		m_pTnow = m_pTracker[m_iTracker];
		m_pTnew = m_pTracker[1-m_iTracker];
	}

	iName = "";
	pK->v("APcopter_base", &iName);
	m_pAP = (APcopter_base*) (pK->parent()->getChildInst(iName));
	IF_Fl(!m_pAP, iName + ": not found");

	iName = "";
	pK->v("_ObjectBase", &iName);
	m_pDet = (_ObjectBase*) (pK->root()->getChildInst(iName));
	IF_Fl(!m_pDet, iName + ": not found");

	iName = "";
	pK->v("APcopter_posCtrlBase", &iName);
	m_pPC = (APcopter_posCtrlBase*) (pK->parent()->getChildInst(iName));
	IF_Fl(!m_pPC, iName + ": not found");

	return true;
}

int APcopter_follow::check(void)
{
	NULL__(m_pAP,-1);
	NULL__(m_pAP->m_pMavlink,-1);
	NULL__(m_pPC,-1);
	NULL__(m_pDet,-1);
	_VisionBase* pV = m_pDet->m_pVision;
	NULL__(pV,-1);
	if(m_bUseTracker)
	{
		NULL__(m_pTracker[0],-1);
		NULL__(m_pTracker[1],-1);
	}

	return this->ActionBase::check();
}

void APcopter_follow::update(void)
{
	this->ActionBase::update();
	IF_(check()<0);
	if(!bActive())
	{
		m_pPC->setPos(m_pPC->m_vTarget);
		m_pDet->goSleep();
		if(m_bUseTracker)
		{
			m_pTracker[0]->stopTrack();
			m_pTracker[1]->stopTrack();
		}

		return;
	}

	if(m_bMissionChanged)
	{
		m_pDet->wakeUp();
	}

	updateGimbal();
	if(!find())
	{
		m_pPC->setPos(m_pPC->m_vTarget);
		return;
	}

	m_pPC->setPos(m_vTarget);
}

bool APcopter_follow::find(void)
{
	IF__(check()<0, false);

	OBJECT* pO;
	OBJECT* tO = NULL;
	double topProb = 0.0;
	int i=0;
	while((pO = m_pDet->at(i++)) != NULL)
	{
		IF_CONT(pO->m_topClass != m_iClass);
		IF_CONT(pO->m_topProb < topProb);

		tO = pO;
		topProb = pO->m_topProb;
	}

	vDouble4 bb;
	if(m_bUseTracker)
	{
		if(tO)
		{
			if(m_pTnew->trackState() == track_stop)
			{
				m_pTnew->startTrack(tO->m_bb);
			}
		}

		if(m_pTnew->trackState() == track_update)
		{
			m_iTracker = 1 - m_iTracker;
			m_pTnow = m_pTracker[m_iTracker];
			m_pTnew = m_pTracker[1-m_iTracker];
			m_pTnew->stopTrack();
		}

		if(m_pTnow->trackState() != track_update)
		{
			m_pMC->transit("search");
			return false;
		}

		bb = *m_pTnow->getBB();
	}
	else
	{
		if(!tO)
		{
			m_pMC->transit("search");
			return false;
		}

		bb = tO->m_bb;
	}

	m_vTarget.x = bb.midX();
	m_vTarget.y = bb.midY();
	m_pMC->transit("follow");

	return true;
}

void APcopter_follow::updateGimbal(void)
{
	m_pAP->m_pMavlink->mountConfigure(m_mountConfig);
	m_pAP->m_pMavlink->mountControl(m_mountControl);

	mavlink_param_set_t D;
	D.param_type = MAV_PARAM_TYPE_INT8;
	string id;

	D.param_value = m_mountConfig.stab_pitch;
	id = "MNT_STAB_TILT";
	strcpy(D.param_id, id.c_str());
	m_pAP->m_pMavlink->param_set(D);

	D.param_value = m_mountConfig.stab_roll;
	id = "MNT_STAB_ROLL";
	strcpy(D.param_id,id.c_str());
	m_pAP->m_pMavlink->param_set(D);
}

bool APcopter_follow::draw(void)
{
	IF_F(!this->ActionBase::draw());
	Window* pWin = (Window*) this->m_pWindow;
	Mat* pMat = pWin->getFrame()->m();
	IF_F(pMat->empty());
	IF_F(check()<0);

	pWin->tabNext();

	if(!bActive())
		pWin->addMsg("Inactive");

	pWin->addMsg("Target = (" + f2str(m_vTarget.x) + ", "
							   + f2str(m_vTarget.y) + ", "
					           + f2str(m_vTarget.z) + ", "
				           	   + f2str(m_vTarget.w) + ")");

	pWin->tabPrev();

	return true;
}

bool APcopter_follow::console(int& iY)
{
	IF_F(!this->ActionBase::console(iY));
	IF_F(check()<0);

	string msg;

	if(!bActive())
	{
		C_MSG("Inactive");
	}

	C_MSG("Target = (" + f2str(m_vTarget.x) + ", "
				     	 + f2str(m_vTarget.y) + ", "
						 + f2str(m_vTarget.z) + ", "
						 + f2str(m_vTarget.w) + ")");

	return true;
}

}
