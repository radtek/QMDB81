#include "Helper/mdbDateTime.h"
#include "Control/mdbHashIndexCtrl.h" 
#include "Control/mdbRowCtrl.h"
#include "Control/mdbMgrShm.h"
#include "Control/mdbLinkCtrl.h" 

//namespace QuickMDB{

    /******************************************************************************
    * 函数名称	:  TMdbHashIndexCtrl
    * 函数描述	:  索引控制模块，构造
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:
    * 作者		:  jin.shaohua
    *******************************************************************************/
    TMdbHashIndexCtrl::TMdbHashIndexCtrl():
        m_pAttachTable(NULL),
        m_pMdbShmDsn(NULL),
        m_pMdbDsn(NULL),
        m_pRowCtrl(NULL)
    {
        m_lSelectIndexValue = -1;
    }

    /******************************************************************************
    * 函数名称	:  ~TMdbHashIndexCtrl
    * 函数描述	:  索引控制模块，析构
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:
    * 作者		:  jin.shaohua
    *******************************************************************************/
    TMdbHashIndexCtrl::~TMdbHashIndexCtrl()
    {
		SAFE_DELETE(m_pRowCtrl);
    }


	int TMdbHashIndexCtrl::DeleteIndexNode(TMdbRowIndex& tRowIndex,ST_HASH_INDEX_INFO& tHashIndexInfo,TMdbRowID& rowID)
    {
        TADD_FUNC("rowID[%d|%d]",rowID.GetPageID(),rowID.GetDataOffset());
        int iRet = 0;
        CHECK_RET(m_pAttachTable->tTableMutex.Lock(m_pAttachTable->bWriteLock,&m_pMdbDsn->tCurTime),"Lock failed.");
        CHECK_RET(FindRowIndexCValue(tHashIndexInfo, tRowIndex,rowID),"FindRowIndexCValue Failed");//查找冲突索引值
        TMdbIndexNode * pBaseNode = &(tHashIndexInfo.pBaseIndexNode[tRowIndex.iBaseIndexPos]);
        TMdbIndexNode * pDataNode = NULL;
        if(false == tRowIndex.IsCurNodeInConflict())
        {//基础链上
            pDataNode = pBaseNode;
        }
        else
        {//冲突链上
            pDataNode = &(tHashIndexInfo.pConflictIndexNode[tRowIndex.iConflictIndexPos]);
        }
        TADD_DETAIL("pDataNode,rowid[%d|%d],NextPos[%d]",
                            pDataNode->tData.GetPageID(),pDataNode->tData.GetDataOffset(),pDataNode->iNextPos);
        //判断rowid是否正确
        if(pDataNode->tData == rowID)
        {
            if(tRowIndex.iConflictIndexPos < 0)
            {//基础链上直接清理下
                TADD_DETAIL("Row in BaseIndex.");
                pDataNode->tData.Clear();//只做下清理就可以了
            }
            else
            {//冲突链上
                TMdbIndexNode * pPreNode = tRowIndex.IsPreNodeInConflict()?&(tHashIndexInfo.pConflictIndexNode[tRowIndex.iPreIndexPos]):pBaseNode;//获取前置节点
                pPreNode->iNextPos  = pDataNode->iNextPos;// 跳过该节点
                //将节点删除
                pDataNode->iNextPos = tHashIndexInfo.pConflictIndex->iFreeHeadPos;
                tHashIndexInfo.pConflictIndex->iFreeHeadPos = tRowIndex.iConflictIndexPos;
                tHashIndexInfo.pConflictIndex->iFreeNodeCounts ++;//剩余节点数-1
                pDataNode->tData.Clear();
            }
        }
        else
        {//error
            TADD_WARNING("not find indexnode to delete....index[%d|%d],rowid[%d|%d]",
                         tRowIndex.iBaseIndexPos,tRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset());
            //PrintIndexInfo(tHashIndexInfo,1,false);
        }

        CHECK_RET(m_pAttachTable->tTableMutex.UnLock(m_pAttachTable->bWriteLock),"Unlock failed.");
        TADD_FUNC("Finish.");
        return iRet;
    }


    /******************************************************************************
    * 函数名称	:  DeleteIndexNode
    * 函数描述	:  删除索引节点
    * 输入		:  iIndexPos - 表上的第几条索引
    * 输入		:  iIndexValue -索引节点值
    * 输入		:  rowID -要删除的记录
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::DeleteIndexNode2(int iIndexPos,TMdbRowIndex& tRowIndex,ST_HASH_INDEX_INFO& tHashIndexInfo,TMdbRowID& rowID)
    {
        TADD_FUNC("rowID[%d|%d]",rowID.GetPageID(),rowID.GetDataOffset());
        int iRet = 0;

		TMdbIndexNode * pBaseNode = &(tHashIndexInfo.pBaseIndexNode[tRowIndex.iBaseIndexPos]);
	    TMdbIndexNode * pDataNode = NULL;
		int iMutexPos = tRowIndex.iBaseIndexPos % tHashIndexInfo.pMutex->iMutexCnt;
        TMiniMutex* pMutex = &(tHashIndexInfo.pMutexNode[iMutexPos]);
		
		CHECK_RET(pMutex->Lock(true),"lock failed.");
        do
        {
	        CHECK_RET_BREAK(FindRowIndexCValue(tHashIndexInfo, tRowIndex,rowID),"FindRowIndexCValue Failed");//查找冲突索引值

	        if(false == tRowIndex.IsCurNodeInConflict())
	        {//基础链上
	            pDataNode = pBaseNode;
	        }
	        else
	        {//冲突链上
	            pDataNode = &(tHashIndexInfo.pConflictIndexNode[tRowIndex.iConflictIndexPos]);
	        }
	        TADD_DETAIL("pDataNode,rowid[%d|%d],NextPos[%d]",pDataNode->tData.GetPageID(),pDataNode->tData.GetDataOffset(),pDataNode->iNextPos);
	        //判断rowid是否正确
	        if(pDataNode->tData == rowID)
	        {
	            if(tRowIndex.iConflictIndexPos < 0)
	            {//基础链上直接清理下
	                TADD_DETAIL("Row in BaseIndex.");
	                pDataNode->tData.Clear();//只做下清理就可以了
	            }
	            else//冲突链上
	            {
		            TMdbSingleTableIndexInfo* pTableIndexInfo = m_pLink->FindCurTableIndex(m_pAttachTable->sTableName);
					CHECK_OBJ_BREAK(pTableIndexInfo);
					
					ST_LINK_INDEX_INFO &tLinkIndexInfo = pTableIndexInfo->arrLinkIndex[iIndexPos];
	                TMdbIndexNode * pPreNode = tRowIndex.IsPreNodeInConflict()?&(tHashIndexInfo.pConflictIndexNode[tRowIndex.iPreIndexPos]):pBaseNode;//获取前置节点
	                pPreNode->iNextPos  = pDataNode->iNextPos;// 跳过该节点
	                
	                //将节点删除,还给链接
	                pDataNode->iNextPos = tLinkIndexInfo.iHashCFreeHeadPos;
	                tLinkIndexInfo.iHashCFreeHeadPos = tRowIndex.iConflictIndexPos;
	                tLinkIndexInfo.iHashCFreeNodeCounts++;
	                pDataNode->tData.Clear();

					CHECK_RET(ReturnConflictNodeToTable(tLinkIndexInfo, tHashIndexInfo),"ReturnConflictNodeToTable Failed");
	            }
	        }
	        else
	        {//error
	            TADD_WARNING("not find indexnode to delete....index[%d|%d],rowid[%d|%d]",tRowIndex.iBaseIndexPos,tRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset());
	            PrintIndexInfo(tHashIndexInfo,0,false);
	        }
        }
		while(0);		
		CHECK_RET(pMutex->UnLock(true),"lock failed.");

        TADD_FUNC("Finish.");
        return iRet;
    }

	int TMdbHashIndexCtrl::DeleteRedirectIndexNode(int iIndexPos,char* pAddr,TMdbRowIndex & tRowIndex,ST_HASH_INDEX_INFO & tHashIndex,TMdbRowID& rowID)
	{
	    TADD_FUNC("IndexPos[%d],tRowIndex[%d|%d],rowID[%d|%d]",iIndexPos,tRowIndex.iBaseIndexPos,tRowIndex.iConflictIndexPos,
	                                                                    rowID.GetPageID(),rowID.GetDataOffset());
		int iRet = 0;

		bool bBase = false;
        long long iIndexNodePos = 0;
        CHECK_RET(m_pRowCtrl->GetIndexPos(pAddr,m_pAttachTable->tIndex[iIndexPos].iRIdxOffset,iIndexPos,bBase,iIndexNodePos),
            "Get index node pos from data failed.index=[%s]", m_pAttachTable->tIndex[iIndexPos].sName);
        if(!bBase)
        {
            tRowIndex.iConflictIndexPos = static_cast<int>(iIndexNodePos);
        }
        else
        {
            tRowIndex.iConflictIndexPos = -1;
        }

        CHECK_RET(m_pAttachTable->tTableMutex.Lock(m_pAttachTable->bWriteLock,&m_pMdbDsn->tCurTime),"Lock failed.");
	    if(false == tRowIndex.IsCurNodeInConflict())
	    {//基础链上
	        TMdbIndexNode * pBaseNode = &(tHashIndex.pBaseIndexNode[tRowIndex.iBaseIndexPos]);

	        if(pBaseNode->tData == rowID)
	        {
	            TADD_DETAIL("Row in BaseIndex.");
	            pBaseNode->tData.Clear();//只做下清理就可以了
	            TADD_DETAIL("delete on base[%d]",tRowIndex.iBaseIndexPos);
	        }
	        else
	        {//error
	            TADD_WARNING("not find indexnode to delete....pos[%d],index[%d|%d],rowid[%d|%d]",
	                         iIndexPos,tRowIndex.iBaseIndexPos,tRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset());
	            //PrintIndexInfo(tHashIndex,1,false);
	        }
	    }
	    else
	    {//冲突链上
	        TMdbReIndexNode* pDataNode = &(tHashIndex.pReConfNode[tRowIndex.iConflictIndexPos]);
	        TADD_DETAIL("to delete on conflict[%d], pre[%s][%d],next[%d]",tRowIndex.iConflictIndexPos, 
	            pDataNode->bPreBase?"BASE":"CONF", pDataNode->iPrePos, pDataNode->iNextPos);
	        if(pDataNode->tData == rowID)
	        {            
	            if(pDataNode->bPreBase) // 前一个节点在基础链上
	            {
	                TMdbIndexNode * pPreNode = &(tHashIndex.pBaseIndexNode[tRowIndex.iBaseIndexPos]);
	                pPreNode->iNextPos = pDataNode->iNextPos;

	                if(pDataNode->iNextPos >= 0)
	                {
	                    TMdbReIndexNode* pNextNode = &(tHashIndex.pReConfNode[pDataNode->iNextPos]);
	                    pNextNode->SetPreNode(tRowIndex.iBaseIndexPos,true);
	                }
	            }
	            else
	            {
	                TMdbReIndexNode* pPreNode = &(tHashIndex.pReConfNode[pDataNode->iPrePos]);
	                pPreNode->iNextPos = pDataNode->iNextPos;
	                if(pDataNode->iNextPos >= 0)
	                {
	                    TMdbReIndexNode* pNextNode = &(tHashIndex.pReConfNode[pDataNode->iNextPos]);
	                    pNextNode->SetPreNode(pDataNode->iPrePos,false);
	                }
	            }

	            pDataNode->iNextPos = tHashIndex.pConflictIndex->iFreeHeadPos;
	            pDataNode->SetPreNode(-1,false);
	            tHashIndex.pConflictIndex->iFreeHeadPos = tRowIndex.iConflictIndexPos;
	            tHashIndex.pConflictIndex->iFreeNodeCounts ++;

	            pDataNode->tData.Clear();

	        }
	        else
	        {//error
	            TADD_WARNING("not find indexnode to delete....pos[%d],index[%d|%d],rowid[%d|%d]",
	                         iIndexPos,tRowIndex.iBaseIndexPos,tRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset());
	            //PrintIndexInfo(tHashIndex,1,false);
	        }
	    }
        CHECK_RET(m_pAttachTable->tTableMutex.UnLock(m_pAttachTable->bWriteLock),"unlock failed.");
	    
	    TADD_FUNC("Finish.");
	    return iRet;
	}

	int TMdbHashIndexCtrl::UpdateIndexNode(TMdbRowIndex& tOldRowIndex,TMdbRowIndex& tNewRowIndex,ST_HASH_INDEX_INFO& tHashInfo,TMdbRowID& tRowId)
    {
        TADD_FUNC("Startrow[%d|%d]",tRowId.GetPageID(),tRowId.GetDataOffset());
        int iRet = 0;

        CHECK_RET(DeleteIndexNode( tOldRowIndex,tHashInfo,tRowId),"DeleteIndexNode failed ,tOldRowIndex[%d|%d],row[%d|%d]",
                  tOldRowIndex.iBaseIndexPos,tOldRowIndex.iConflictIndexPos,tRowId.GetPageID(),tRowId.GetDataOffset());//先删除
        CHECK_RET(InsertIndexNode( tNewRowIndex,tHashInfo,tRowId),"InsertIndexNode failed ,tNewRowIndex[%d|%d],row[%d|%d]",
                  tNewRowIndex.iBaseIndexPos,tNewRowIndex.iConflictIndexPos,tRowId.GetPageID(),tRowId.GetDataOffset());//再增加
                  
        TADD_FUNC("Finish.");
        return iRet;
    }

    /******************************************************************************
    * 函数名称	:  UpdateIndexNode
    * 函数描述	:  更新索引节点
    * 输入		:  iIndexPos - 表上的第几条索引
    * 输入		:  iOldValue - 旧值，iNewValue -新值
    * 输出		:  rowID -要更新的记录
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::UpdateIndexNode2(int iIndexPos,TMdbRowIndex& tOldRowIndex,TMdbRowIndex& tNewRowIndex,ST_HASH_INDEX_INFO& tHashInfo,TMdbRowID& tRowId)
    {
        TADD_FUNC("Startrow[%d|%d]",tRowId.GetPageID(),tRowId.GetDataOffset());
        int iRet = 0;

        CHECK_RET(DeleteIndexNode2(iIndexPos,tOldRowIndex,tHashInfo,tRowId),"DeleteIndexNode2 failed ,tOldRowIndex[%d|%d],row[%d|%d]",
                  tOldRowIndex.iBaseIndexPos,tOldRowIndex.iConflictIndexPos,tRowId.GetPageID(),tRowId.GetDataOffset());//先删除
        CHECK_RET(InsertIndexNode2(iIndexPos,tNewRowIndex,tHashInfo,tRowId),"InsertIndexNode2 failed ,tNewRowIndex[%d|%d],row[%d|%d]",
                  tNewRowIndex.iBaseIndexPos,tNewRowIndex.iConflictIndexPos,tRowId.GetPageID(),tRowId.GetDataOffset());//再增加
                  
        TADD_FUNC("Finish.");
        return iRet;
    }

	
    int TMdbHashIndexCtrl::UpdateRedirectIndexNode(int iIndexPos,char* pAddr,TMdbRowIndex& tOldRowIndex,TMdbRowIndex& tNewRowIndex,ST_HASH_INDEX_INFO& tHashIndex,TMdbRowID& rowID)
    {
		int iRet = 0;

        CHECK_RET(DeleteRedirectIndexNode(iIndexPos,pAddr, tOldRowIndex,tHashIndex,rowID),"DeleteIndexNode2 failed ,tOldRowIndex[%d|%d],row[%d|%d]",
                  tOldRowIndex.iBaseIndexPos,tOldRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset());//先删除
        CHECK_RET(InsertRedirectIndexNode(iIndexPos,pAddr, tNewRowIndex,tHashIndex,rowID),"InsertRedirectIndexNode failed ,tNewRowIndex[%d|%d],row[%d|%d]",
                  tNewRowIndex.iBaseIndexPos,tNewRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset());//再增加
		return iRet;
	}


	int TMdbHashIndexCtrl::InsertIndexNode(TMdbRowIndex& tRowIndex,ST_HASH_INDEX_INFO& tHashIndex, TMdbRowID& rowID)
    {
        TADD_FUNC("Start.rowID[%d|%d]",  rowID.GetPageID(),rowID.GetDataOffset());
        int iRet = 0;
        CHECK_RET(m_pAttachTable->tTableMutex.Lock(m_pAttachTable->bWriteLock,&m_pMdbDsn->tCurTime),"Lock failed.");
        TMdbIndexNode * pBaseNode = &(tHashIndex.pBaseIndexNode[tRowIndex.iBaseIndexPos]);
        tRowIndex.iPreIndexPos       = -1;
        if(pBaseNode->tData.IsEmpty())
        {
            //基础索引链上没有值
            TADD_DETAIL("Row can insert BaseIndex.");
            pBaseNode->tData = rowID;
            tRowIndex.iConflictIndexPos = -1;
        }
        else
        {
            TADD_DETAIL("Row can insert ConflictIndex.");
            if(tHashIndex.pConflictIndex->iFreeHeadPos >= 0)
            {
                //还有空闲冲突节点, 对冲突链进行头插
                int iFreePos = tHashIndex.pConflictIndex->iFreeHeadPos;
                TMdbIndexNode * pFreeNode = &(tHashIndex.pConflictIndexNode[iFreePos]);//空闲冲突节点
                pFreeNode->tData = rowID;//放入数据
                tHashIndex.pConflictIndex->iFreeHeadPos = pFreeNode->iNextPos;
                pFreeNode->iNextPos  = pBaseNode->iNextPos;
                pBaseNode->iNextPos  = iFreePos;
                tHashIndex.pConflictIndex->iFreeNodeCounts --;//剩余节点数-1
                tRowIndex.iConflictIndexPos = iFreePos;//在冲突链上某位置
            }
            else
            {
                //没有空闲冲突节点了。
                TADD_ERROR(ERR_TAB_NO_CONFLICT_INDEX_NODE,"No free conflict indexnode.....tRowIndex[%d|%d],rowid[%d|%d],record-counts[%d] of table[%s] is too small",
                           tRowIndex.iBaseIndexPos,tRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset(),
                           m_pAttachTable->iRecordCounts,m_pAttachTable->sTableName);
                //PrintIndexInfo(tHashIndex,0,false);
                iRet = ERR_TAB_NO_CONFLICT_INDEX_NODE;
            }
        }
        CHECK_RET(m_pAttachTable->tTableMutex.UnLock(m_pAttachTable->bWriteLock),"unlock failed.");
        TADD_FUNC("Finish.");
        return iRet;
    }
	
	
    /******************************************************************************
    * 函数名称	:  InsertIndexNode
    * 函数描述	:  插入索引节点
    * 输入		:  iIndexPos - 表上的第几条索引
    * 输入		:  iIndexValue -索引值
    * 输出		:  rowID -要插入的记录
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::InsertIndexNode2(int iIndexPos,TMdbRowIndex& tRowIndex,ST_HASH_INDEX_INFO& tTableHashIndex, TMdbRowID& rowID)
    {
        TADD_FUNC("Start.rowID[%d|%d]",  rowID.GetPageID(),rowID.GetDataOffset());
        int iRet = 0;
        TMdbIndexNode * pBaseNode = &(tTableHashIndex.pBaseIndexNode[tRowIndex.iBaseIndexPos]);
        tRowIndex.iPreIndexPos       = -1;

		int iMutexPos = tRowIndex.iBaseIndexPos % tTableHashIndex.pMutex->iMutexCnt;
        TMiniMutex* pMutex = &(tTableHashIndex.pMutexNode[iMutexPos]);

		
		CHECK_RET(pMutex->Lock(true),"lock failed.");
        do
        {
		    if(pBaseNode->tData.IsEmpty())
		    {
		        //基础索引链上没有值
		        TADD_DETAIL("Row can insert BaseIndex.");
		        pBaseNode->tData = rowID;
		        tRowIndex.iConflictIndexPos = -1;
		    }
		    else
		    {
		    	TMdbSingleTableIndexInfo* pTableIndexInfo = m_pLink->FindCurTableIndex(m_pAttachTable->sTableName);
				CHECK_OBJ(pTableIndexInfo);
				
				ST_LINK_INDEX_INFO &tLinkIndexInfo = pTableIndexInfo->arrLinkIndex[iIndexPos];

				int iTry = 3;
				do
				{
					if(tLinkIndexInfo.iHashCFreeHeadPos>= 0)
		            {
		                //还有空闲冲突节点, 对冲突链进行头插
		                int iFreePos = tLinkIndexInfo.iHashCFreeHeadPos;
		                TMdbIndexNode * pFreeNode = &(tTableHashIndex.pConflictIndexNode[iFreePos]);//空闲冲突节点
		                pFreeNode->tData = rowID;//放入数据
		                tLinkIndexInfo.iHashCFreeHeadPos = pFreeNode->iNextPos;
		                pFreeNode->iNextPos  = pBaseNode->iNextPos;
		                pBaseNode->iNextPos  = iFreePos;
		                tLinkIndexInfo.iHashCFreeNodeCounts --;//剩余节点数-1
		                tRowIndex.iConflictIndexPos = iFreePos;//在冲突链上某位置
		                break;
		            }
		            else
		            {
		            	//链接上的冲突节点不够，从表申请
		            	iRet = ApplyConflictNodeFromTable(tLinkIndexInfo, tTableHashIndex);

						if(iRet!=0){iTry--;}
						
						//最多尝试三次，要是还申请不到，就报错
						if(iTry < 0)
						{
			                TADD_ERROR(ERR_TAB_NO_CONFLICT_INDEX_NODE,"No free conflict indexnode.....tRowIndex[%d|%d],rowid[%d|%d],record-counts[%d] of table[%s] is too small",
			                           tRowIndex.iBaseIndexPos,tRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset(),
			                           m_pAttachTable->iRecordCounts,m_pAttachTable->sTableName);
			                PrintIndexInfo(tTableHashIndex,0,false);
			                iRet = ERR_TAB_NO_CONFLICT_INDEX_NODE;
							break;
						}
		            }
				}while(1);	
		    }
        }
        while(0);
        CHECK_RET(pMutex->UnLock(true), "unlock failed.");  
		
        TADD_FUNC("Finish.");
        return iRet;
    }
	int TMdbHashIndexCtrl::InsertRedirectIndexNode(int iIndexPos,char* pAddr,TMdbRowIndex &tRowIndex, ST_HASH_INDEX_INFO & tHashIndex, TMdbRowID& rowID)
	{
	    int iRet = 0;
	    TADD_FUNC("tRowIndex[%d|%d],rowID[%d|%d]",tRowIndex.iBaseIndexPos,tRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset());
        CHECK_RET(m_pAttachTable->tTableMutex.Lock(m_pAttachTable->bWriteLock,&m_pMdbDsn->tCurTime),"Lock failed.");
	    TMdbIndexNode * pBaseNode = &(tHashIndex.pBaseIndexNode[tRowIndex.iBaseIndexPos]);
	    tRowIndex.iPreIndexPos       = -1;
	    if(pBaseNode->tData.IsEmpty())
	    {
	        //基础索引链上没有值
	        TADD_DETAIL("Row can insert BaseIndex.");
	        pBaseNode->tData = rowID;
	        tRowIndex.iConflictIndexPos = -1;//直接放在基础链上
	        TADD_DETAIL("insert on base[%d]",tRowIndex.iBaseIndexPos);
	    }
	    else
	    {
	        TADD_DETAIL("Row can insert ConflictIndex.");
	        if(tHashIndex.pConflictIndex->iFreeHeadPos >= 0)
	        {
	            //还有空闲冲突节点, 对冲突链进行头插
	            int iFreePos = tHashIndex.pConflictIndex->iFreeHeadPos;
	            TMdbReIndexNode * pFreeNode = &(tHashIndex.pReConfNode[iFreePos]);//空闲冲突节点
	            pFreeNode->tData = rowID;//放入数据
	            tHashIndex.pConflictIndex->iFreeHeadPos = pFreeNode->iNextPos;
	            if(pBaseNode->iNextPos >= 0)
	            {//有链
	                TMdbReIndexNode * pNextNode = &(tHashIndex.pReConfNode[pBaseNode->iNextPos]);
	                pNextNode->SetPreNode(iFreePos,false);
	            }
	            pFreeNode->iNextPos  = pBaseNode->iNextPos;
	            pFreeNode->SetPreNode(tRowIndex.iBaseIndexPos,true);
	            pBaseNode->iNextPos  = iFreePos;
	            tHashIndex.pConflictIndex->iFreeNodeCounts --;//剩余节点数-1
	            tRowIndex.iConflictIndexPos = iFreePos;//在冲突链上某位置
	            TADD_DETAIL("insert on conflict[%d], pre:[%s][%d], next[%d]"
	                ,iFreePos, pFreeNode->bPreBase?"BASE":"CONF",pFreeNode->iPrePos,pFreeNode->iNextPos);
	            
	        }
	        else
	        {
	            //没有空闲冲突节点了。
	            TADD_ERROR(ERR_TAB_NO_CONFLICT_INDEX_NODE,"No free conflict indexnode.....tRowIndex[%d|%d],rowid[%d|%d],record-counts[%d] of table[%s] is too small",
	                       tRowIndex.iBaseIndexPos,tRowIndex.iConflictIndexPos,rowID.GetPageID(),rowID.GetDataOffset(),m_pAttachTable->iRecordCounts,m_pAttachTable->sTableName);
                //PrintIndexInfo(tHashIndex,0,false);
	            iRet = ERR_TAB_NO_CONFLICT_INDEX_NODE;
	        }
	    }
        CHECK_RET(m_pAttachTable->tTableMutex.UnLock(m_pAttachTable->bWriteLock),"unlock failed.");

		//插入完索引，需要在记录里面打上标记
		if(tRowIndex.IsCurNodeInConflict())
        {
            m_pRowCtrl->SetIndexPos(pAddr,m_pAttachTable->tIndex[iIndexPos].iRIdxOffset,iIndexPos, false,tRowIndex.iConflictIndexPos);
        }
        else
        {
            m_pRowCtrl->SetIndexPos(pAddr,m_pAttachTable->tIndex[iIndexPos].iRIdxOffset,iIndexPos, true,tRowIndex.iBaseIndexPos);
        }
		
	    TADD_FUNC("Finish.");
	    return iRet;
	}

	int TMdbHashIndexCtrl::ApplyConflictNodeFromTable(ST_LINK_INDEX_INFO& tLinkIndexInfo, ST_HASH_INDEX_INFO& tTableHashIndex)
    {
    	int iRet = 0;
		//tTableHashIndex 属于表级别的公共资源，需要加锁保护
        CHECK_RET(m_pAttachTable->tTableMutex.Lock(m_pAttachTable->bWriteLock,&m_pMdbDsn->tCurTime),"Lock failed.");
		int iAskNodeNum = GetCApplyNum(m_pAttachTable->iTabLevelCnts);

		do
		{
			if(tTableHashIndex.pConflictIndex->iFreeHeadPos < 0) 
			{
				iRet = ERR_TAB_NO_CONFLICT_INDEX_NODE;
				break;
			}

			tLinkIndexInfo.iHashCFreeHeadPos= tTableHashIndex.pConflictIndex->iFreeHeadPos;
			
			int iFreePos = -1;	
			int iLastPos = -1;//记录申请到的最后一个节点的位置
			for(int i = 0;i<iAskNodeNum;i++)
			{	
				iFreePos = tTableHashIndex.pConflictIndex->iFreeHeadPos;
				//表上面的冲突节点也申请完了
				if(-1 == iFreePos){break;}
				
				TMdbIndexNode * pFreeNode = &(tTableHashIndex.pConflictIndexNode[iFreePos]);

				//削减表的冲突节点长度
				tTableHashIndex.pConflictIndex->iFreeHeadPos = pFreeNode->iNextPos;
				tTableHashIndex.pConflictIndex->iFreeNodeCounts--;
				
				tLinkIndexInfo.iHashCFreeNodeCounts++;
				iLastPos = iFreePos;

			}

			if(-1 != iLastPos)
			{
				TMdbIndexNode* pLastNode = &(tTableHashIndex.pConflictIndexNode[iLastPos]);
				pLastNode->iNextPos = -1;
			}

		}while(0);

		//TADD_NORMAL("Link Ask %d nodes From Table %s.",tLinkIndexInfo.iHashCFreeNodeCounts,m_pAttachTable->sTableName);

		CHECK_RET(m_pAttachTable->tTableMutex.UnLock(m_pAttachTable->bWriteLock),"unlock failed.");
		return iRet;
	}


	
	int TMdbHashIndexCtrl::ReturnConflictNodeToTable(ST_LINK_INDEX_INFO& tLinkIndexInfo, ST_HASH_INDEX_INFO& tTableHashIndex)
	{
		int iRet = 0;
		int iNum = GetCApplyNum(m_pAttachTable->iTabLevelCnts);
		//只有链接上存在过多的节点，才会归还部分
		if(tLinkIndexInfo.iHashCFreeNodeCounts > 2* iNum)
		{			
			CHECK_RET(m_pAttachTable->tTableMutex.Lock(m_pAttachTable->bWriteLock,&m_pMdbDsn->tCurTime),"Lock failed.");
			int iReturnNum = iNum;
			int iFreePosOfTable = tTableHashIndex.pConflictIndex->iFreeHeadPos;			
			tTableHashIndex.pConflictIndex->iFreeHeadPos = tLinkIndexInfo.iHashCFreeHeadPos;
			TMdbIndexNode * pFreeNode = NULL;
			
			//将链表的前面iReturnNum个节点归还
			for(int i=0; i<iReturnNum; i++)
			{
				pFreeNode = &(tTableHashIndex.pConflictIndexNode[tLinkIndexInfo.iHashCFreeHeadPos]);
				tLinkIndexInfo.iHashCFreeHeadPos = pFreeNode->iNextPos;
				tLinkIndexInfo.iHashCFreeNodeCounts --;
			}
			pFreeNode->iNextPos = iFreePosOfTable;
			tTableHashIndex.pConflictIndex->iFreeNodeCounts +=iReturnNum;
			
			CHECK_RET(m_pAttachTable->tTableMutex.UnLock(m_pAttachTable->bWriteLock),"unlock failed.");
			
		}


		return iRet;

	}

	
	int  TMdbHashIndexCtrl::ReturnAllIndexNodeToTable(ST_LINK_INDEX_INFO& tLinkIndexInfo, ST_HASH_INDEX_INFO& tTableHashIndex)
	{
		int iRet = 0;

		if(tLinkIndexInfo.iHashCFreeNodeCounts <=0) return iRet;
		
		CHECK_RET(m_pAttachTable->tTableMutex.Lock(m_pAttachTable->bWriteLock,&m_pMdbDsn->tCurTime),"Lock failed.");

		int iFreePosOfTable = tTableHashIndex.pConflictIndex->iFreeHeadPos;			
		tTableHashIndex.pConflictIndex->iFreeHeadPos = tLinkIndexInfo.iHashCFreeHeadPos;
		TMdbIndexNode * pFreeNode = NULL;
		
		while(tLinkIndexInfo.iHashCFreeNodeCounts > 0)
		{
			pFreeNode = &(tTableHashIndex.pConflictIndexNode[tLinkIndexInfo.iHashCFreeHeadPos]);
			tLinkIndexInfo.iHashCFreeHeadPos = pFreeNode->iNextPos;
			tLinkIndexInfo.iHashCFreeNodeCounts --;
			tTableHashIndex.pConflictIndex->iFreeNodeCounts++;
		}
		
		pFreeNode->iNextPos = iFreePosOfTable;
		
		CHECK_RET(m_pAttachTable->tTableMutex.UnLock(m_pAttachTable->bWriteLock),"unlock failed.");		
		return iRet;
	}
	    /******************************************************************************
    * 函数名称	:  FindRowIndexCValue
    * 函数描述	:  查找冲突索引值
    * 输入		:  iIndexPos - 表上的第几条索引
    * 输入		:  tRowIndex -记录所对应的索引值
    * 输出		:  rowID -记录的rowid
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::FindRowIndexCValue(ST_HASH_INDEX_INFO tHashIndexInfo,TMdbRowIndex & tRowIndex,TMdbRowID& rowID)
    {
        TADD_FUNC("rowID[%d|%d]", rowID.GetPageID(),rowID.GetDataOffset());
        int iRet = 0;
        int iCount = 3;//找三次
        bool bFind = false;
        while(1)
        {
            TMdbIndexNode * pBaseNode = &(tHashIndexInfo.pBaseIndexNode[tRowIndex.iBaseIndexPos]);
            TMdbIndexNode * pTempNode = pBaseNode;
            int iCurPos = -1;//当前位置值
            tRowIndex.iPreIndexPos = iCurPos;
            do
            {
                if(pTempNode->tData == rowID)
                {//找到
                    tRowIndex.iConflictIndexPos = iCurPos;
                    bFind = true;
                    break;
                }
                if(pTempNode->iNextPos < 0)
                {//找完，没找到
                    TADD_WARNING("Not find iCount = [%d]",iCount);
                    break;
                }
                else
                {//查找下一个
                    tRowIndex.iPreIndexPos = iCurPos;
                    iCurPos = pTempNode->iNextPos;
                    pTempNode = &(tHashIndexInfo.pConflictIndexNode[pTempNode->iNextPos]);
                }
            }while(1);
            if(iCount <= 0 || bFind){break;}//找完了
            iCount --;
        }
        if(false == bFind)
        {//找了N次都没找到
            CHECK_RET(ERR_DB_INVALID_VALUE,"not find index node rowID[%d|%d]",
                                rowID.GetPageID(),rowID.GetDataOffset());
        }
        return iRet;
    }


    /******************************************************************************
    * 函数名称	:  OutPutInfo
    * 函数描述	:  输出信息
    * 输入		:  bConsole - 是否向控制台输出
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::OutPutInfo(bool bConsole,const char * fmt, ...)
    {
        static char sLogTemp[10240];
		memset(sLogTemp,0,sizeof(sLogTemp));
        va_list ap;
        va_start(ap,fmt);
        vsnprintf(sLogTemp, sizeof(sLogTemp), fmt, ap);
        va_end (ap);
        if(bConsole)
        {
            printf("%s",sLogTemp);
        }
        else
        {
            TADD_NORMAL("%s",sLogTemp);
        }
        return 0;
    }



    /******************************************************************************
    * 函数名称	:  PrintIndexInfo
    * 函数描述	:  打印索引信息
    * 输入		:  iDetialLevel - 详细级别 =0 基本信息，>0 详细信息
    * 输入		:  bConsole    - 是否向控制台输出
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::PrintIndexInfo(ST_HASH_INDEX_INFO& tIndexInfo,int iDetialLevel,bool bConsole)
    {
        int iRet = 0;
        size_t iBICounts = tIndexInfo.pBaseIndex->iSize/sizeof(TMdbIndexNode);//基础索引个数
        
		size_t iCICounts = 0;
		bool bReConf = (NULL == tIndexInfo.pReConfNode) ? false :true;
        if(bReConf)
        {
            iCICounts = tIndexInfo.pConflictIndex->iSize/sizeof(TMdbReIndexNode);
        }
        else
        {
            iCICounts = tIndexInfo.pConflictIndex->iSize/sizeof(TMdbIndexNode);//冲突索引个数
        }
        OutPutInfo(bConsole,"\n\n============[%s]===========\n",tIndexInfo.pBaseIndex->sName);		
        OutPutInfo(bConsole,"[IsRedirectIndex] 	 [%d]\n",bReConf);
        OutPutInfo(bConsole,"[BaseIndex] 	 counts=[%lu]\n",iBICounts);
        OutPutInfo(bConsole,"[ConfilictIndex] counts=[%lu],FreeHeadPos=[%d],FreeNodes=[%d]\n",
                   iCICounts,
                   tIndexInfo.pConflictIndex->iFreeHeadPos,
                   tIndexInfo.pConflictIndex->iFreeNodeCounts);
        if(iDetialLevel >0 )
        {//详细信息
       	 	if(bReConf)
            {
                PrintRePosIndexInfoDetail(iDetialLevel,bConsole,tIndexInfo);
            }
			else
			{
           		PrintIndexInfoDetail(iDetialLevel,bConsole,tIndexInfo);
			}
        }
        return iRet;
    }

	
	int TMdbHashIndexCtrl::PrintRePosIndexInfoDetail(int iDetialLevel,bool bConsole, ST_HASH_INDEX_INFO & stIndexInfo)
	{
		size_t arrRange[][3] = {{0,10,0},{10,100,0},{100,500,0},{500,2000,0},{2000,5000,0},{5000, 0 ,0}};//区间范围，值
		size_t iRangeCount = sizeof(arrRange)/(3*sizeof(size_t));
		size_t iBICounts = stIndexInfo.pBaseIndex->iSize/sizeof(TMdbIndexNode);//基础索引个数
		size_t iCICounts = stIndexInfo.pConflictIndex->iSize/sizeof(TMdbReIndexNode);//冲突索引个数

		for(size_t i = 0; i < iBICounts; ++i)
		{//遍历每条链
			size_t iCount = 0;
			int iNextPos = stIndexInfo.pBaseIndexNode[i].iNextPos;
			iCount = 0;
			while(iNextPos >= 0)
			{
				iCount++;
				if(iCount > iBICounts)
				{
					OutPutInfo(bConsole,"\nOMG,unlimited loop...\n");
					return 0;
				}
				iNextPos = stIndexInfo.pReConfNode[iNextPos].iNextPos;
			}
			size_t j = 0;
			for(j = 0;j < iRangeCount;++j)
			{
				if(iCount > arrRange[j][0] &&( iCount <= arrRange[j][1] || arrRange[j][1] == 0))
				{
					arrRange[j][2]++;//落在此区间内
					break;
				}
			}
		}
		OutPutInfo(bConsole,"\nBaseIndex Detail:\n");
		for(size_t i = 0;i< iRangeCount;++i)
		{
			OutPutInfo(bConsole,"[%-6lu ~ %-6lu] counts = [%-6lu]\n",arrRange[i][0],arrRange[i][1],arrRange[i][2]);
		}
		//统计冲突剩余链
		int iNextPos = stIndexInfo.pConflictIndex->iFreeHeadPos;
		size_t iCount = 0;
		while(iNextPos >= 0)
		{
			iCount++;
			if(iCount > iCICounts)
			{
				OutPutInfo(bConsole,"\nOMG,unlimited loop...iCount[%lu],iCICounts[%lu]\n",iCount,iCICounts);
				return 0;
			}
			iNextPos = stIndexInfo.pReConfNode[iNextPos].iNextPos;
		}
		OutPutInfo(bConsole,"free conflict nodes = %lu\n",iCount);
		if(iDetialLevel > 1)
		{
			int i = 0;
			int iPrintCounts = iDetialLevel<(int)iCICounts?iDetialLevel:(int)iCICounts;
			printf("Conflict node detail:\n");
			for(i = 0;i<iPrintCounts;++i)
			{
					if(i%10 == 0){printf("\n");}
					printf("[%d:%d] ",i,stIndexInfo.pReConfNode[i].iNextPos);
			}
			printf("\n");
		}
		return 0;
	}
    /******************************************************************************
    * 函数名称	:  PrintIndexInfoDetail
    * 函数描述	:  打印索引信息
    * 输入		:  iDetialLevel - 详细级别 =0 基本信息，>0 详细信息
    * 输入		:  bConsole    - 是否向控制台输出
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::PrintIndexInfoDetail(int iDetialLevel,bool bConsole, ST_HASH_INDEX_INFO & stIndexInfo)
    {
        size_t arrRange[][3] = {{0,10,0},{10,100,0},{100,500,0},{500,2000,0},{2000,5000,0},{5000,0 ,0}};//区间范围，值
        size_t iRangeCount = sizeof(arrRange)/(3*sizeof(size_t));
        size_t iBICounts = stIndexInfo.pBaseIndex->iSize/sizeof(TMdbIndexNode);//基础索引个数
        size_t iCICounts = stIndexInfo.pConflictIndex->iSize/sizeof(TMdbIndexNode);//冲突索引个数
		size_t i = 0;
        for( i = 0; i < iBICounts; ++i)
        {//遍历每条链
            size_t iCount = 0;
            int iNextPos = stIndexInfo.pBaseIndexNode[i].iNextPos;

            while(iNextPos >= 0)
            {
                iCount++;
                if(iCount > iBICounts)
                {
                    OutPutInfo(bConsole,"\nOMG,unlimited loop...\n");
                    return 0;
                }
                iNextPos = stIndexInfo.pConflictIndexNode[iNextPos].iNextPos;
            }
            size_t j = 0;
            for(j = 0;j < iRangeCount;++j)
            {
                if(iCount > arrRange[j][0] &&( iCount <= arrRange[j][1] || arrRange[j][1]== 0))
                {
                    arrRange[j][2]++;//落在此区间内
                    break;
                }
            }
        }
        OutPutInfo(bConsole,"\nBaseIndex Detail:\n");
        for(  i = 0;i< iRangeCount;++i)
        {
            OutPutInfo(bConsole,"[%-6lu ~ %-6lu] counts = [%-6lu]\n",arrRange[i][0],arrRange[i][1],arrRange[i][2]);
        }
        //统计冲突剩余链
        int iNextPos = stIndexInfo.pConflictIndex->iFreeHeadPos;
        size_t iCount = 0;
        while(iNextPos >= 0)
        {
            iCount++;
            if(iCount > iCICounts)
            {
                OutPutInfo(bConsole,"\nOMG,unlimited loop...iCount[%lu],iCICounts[%lu]\n",iCount,iCICounts);
                return 0;
            }
            iNextPos = stIndexInfo.pConflictIndexNode[iNextPos].iNextPos;
        }
        OutPutInfo(bConsole,"free conflict nodes = %lu\n",iCount);
        if(iDetialLevel > 1)
        {
            int i = 0;
            int iPrintCounts = iDetialLevel<iCICounts?iDetialLevel:(int)iCICounts;
            printf("Conflict node detail:\n");
            for(i = 0;i<iPrintCounts;++i)
            {
                    if(i%10 == 0){printf("\n");}
                    printf("[%d:%d] ",i,stIndexInfo.pConflictIndexNode[i].iNextPos);
            }
            printf("\n");
        }
        return 0;
    }

    /******************************************************************************
    * 函数名称	:  AttachDsn
    * 函数描述	:  连接上共享内存管理区
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::AttachDsn(TMdbShmDSN * pMdbShmDsn)
    {
        int iRet = 0;
        CHECK_OBJ(pMdbShmDsn);
        m_pMdbShmDsn = pMdbShmDsn;
        CHECK_RET(m_pMdbShmDsn->Attach(),"attach failed...");
        m_pMdbDsn = pMdbShmDsn->GetInfo();
        CHECK_OBJ(m_pMdbDsn);
        return iRet;
    }

    int TMdbHashIndexCtrl::AttachTable(TMdbShmDSN * pMdbShmDsn, TMdbTable* pTable)
    {
        int iRet = 0;
        CHECK_OBJ(pTable);
        CHECK_RET(AttachDsn(pMdbShmDsn),"attch dsn failed.");
        m_pAttachTable = pTable;
		SAFE_DELETE(m_pRowCtrl);
		m_pRowCtrl = new (std::nothrow) TMdbRowCtrl;
		CHECK_OBJ(m_pRowCtrl);
		CHECK_RET(m_pRowCtrl->Init(m_pMdbDsn->sName,pTable),"m_pRowCtrl.AttachTable failed.");//记录管理
        return iRet;
    }

	
    int TMdbHashIndexCtrl::RenameTableIndex(TMdbShmDSN * pMdbShmDsn, TMdbTable* pTable, const char *sNewTableName, int& iFindIndexs)
    {
    	int  iRet = 0;
		CHECK_RET(AttachTable(pMdbShmDsn, pTable),"hash index attach table failed.");
		for(int n=0; n<MAX_SHM_ID; ++n)
		{
		    TADD_DETAIL("Attach (%s) hash Index : Shm-ID no.[%d].", pTable->sTableName, n);
		    char * pBaseIndexAddr = pMdbShmDsn->GetBaseIndex(n);
		    if(pBaseIndexAddr == NULL)
		        continue;

		    TMdbBaseIndexMgrInfo *pBIndexMgr = (TMdbBaseIndexMgrInfo*)pBaseIndexAddr;//获取基础索引内容
		    for(int j=0; j<MAX_BASE_INDEX_COUNTS && iFindIndexs<pTable->iIndexCounts; ++j)
		    {
		        if(0 == TMdbNtcStrFunc::StrNoCaseCmp( pTable->sTableName, pBIndexMgr->tIndex[j].sTabName))
		        {
		            iFindIndexs++;
		            SAFESTRCPY(pBIndexMgr->tIndex[j].sTabName,sizeof(pBIndexMgr->tIndex[j].sTabName),sNewTableName);                    
		        }

		        
		    }
		    if(iFindIndexs == pTable->iIndexCounts)
		    {
		        return iRet;
		    }
        
        
    	}	
		return iRet;
	
	}
	
    /******************************************************************************
    * 函数名称	:  InitIndexNode
    * 函数描述	:  初始化索引节点
    * 输入		:  pNode - 索引的头指针
    * 输入		:  iSize   - 索引大小 bList -是否要连成串
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::InitIndexNode(TMdbIndexNode* pNode,MDB_INT64 iSize,bool bList)
    {
        MDB_INT64 iCount = iSize/sizeof(TMdbIndexNode);
        if(bList)
        {
            //需要连成双链表状态
            pNode[iCount - 1].iNextPos = -1;//结尾
            for(MDB_INT64 n=0; n<iCount-1; ++n)
            {
                pNode->tData.Clear();
                pNode->iNextPos = n + 1;//连成串
                ++pNode;
            }
        }
        else
        {
            for(MDB_INT64 n=0; n<iCount; ++n)
            {
                pNode->tData.Clear();
                pNode->iNextPos = -1;
                ++pNode;
            }
        }
        return 0;
    }

int TMdbHashIndexCtrl::InitRePosIndexNode(TMdbReIndexNode* pNode,MDB_INT64 iSize,bool bList)
	{
	    MDB_INT64 iCount = iSize/sizeof(TMdbReIndexNode);
	    if(bList)
	    {
	        //需要连成双链表状态
	        pNode[iCount - 1].iNextPos = -1;//结尾
	        for(MDB_INT64 n=0; n<iCount-1; ++n)
	        {
	            pNode->tData.Clear();
	            pNode->iNextPos = n + 1;//连成串
	            ++pNode;
	        }
	    }
	    else
	    {
	        for(MDB_INT64 n=0; n<iCount; ++n)
	        {
	            pNode->tData.Clear();
	            pNode->iNextPos = -1;
	            ++pNode;
	        }
	    }
	    return 0;
	}	int TMdbHashIndexCtrl::CreateHashNewMutexShm(size_t iShmSize)
    {
        TADD_FUNC("Start.Size=[%lu].",iShmSize);
        int iRet = 0;
        if(MAX_SHM_ID == m_pMdbShmDsn->GetInfo()->iHashMutexShmCnt)
        {
            CHECK_RET(ERR_OS_NO_MEMROY,"can't create new shm,MAX_SHM_COUNTS[%d]",MAX_SHM_ID);
        }
        int iPos = m_pMdbDsn->iHashMutexShmCnt;
        TADD_FLOW("Create mutexShm:[%d],mutex_key[0x%0x]",iPos,m_pMdbDsn->iHashMutexShmKey[iPos]);
        CHECK_RET(TMdbShm::Create(m_pMdbDsn->iHashMutexShmKey[iPos], iShmSize, m_pMdbDsn->iHashMutexShmID[iPos]),
                  " Can't Create mutexIndexShm errno=%d[%s].", errno, strerror(errno));
        TADD_FLOW("Mutex_SHM_ID =[%d].SHM_SIZE=[%lu].",m_pMdbDsn->iHashMutexShmID[iPos],iShmSize);
        TADD_NORMAL_TO_CLI(FMT_CLI_OK,"Create HashMutex IndexShm:[%d],size=[%luMB]",iPos,iShmSize/(1024*1024));
        m_pMdbDsn->iHashMutexShmCnt ++;
        CHECK_RET(m_pMdbShmDsn->ReAttachIndex(),"ReAttachIndex failed....");//对于新创建的shm获取新映射地址
        //初始化索引区信息
        TMdbHashMutexMgrInfo* pMutexMgr = (TMdbHashMutexMgrInfo*)m_pMdbShmDsn->GetHashMutex(iPos);// ->m_pBaseIndexShmAddr[iPos];
        CHECK_OBJ(pMutexMgr);
        pMutexMgr->iSeq = iPos;
        int i = 0;
        for(i = 0; i<MAX_MHASH_INDEX_COUNT; i++)
        {
            pMutexMgr->aBaseMutex[i].Clear();
            pMutexMgr->tFreeSpace[i].Clear();
        }
        pMutexMgr->tFreeSpace[0].iPosAdd = sizeof(TMdbHashMutexMgrInfo);
        pMutexMgr->tFreeSpace[0].iSize   = iShmSize - sizeof(TMdbHashMutexMgrInfo);

        pMutexMgr->iCounts= 0;
        pMutexMgr->iTotalSize   = iShmSize;
        pMutexMgr->tMutex.Create();
        TADD_FUNC("Finish.");
        return iRet;
    }

	int TMdbHashIndexCtrl::GetHashFreeMutexShm(MDB_INT64 iMutexSize,size_t iDataSize,ST_HASH_INDEX_INFO & stTableIndexInfo)
    {
        TADD_FUNC("Start.iMutexSize=[%lld].iDataSize=[%lu]",iMutexSize,iDataSize);
        int iRet = 0;
        if((size_t)iMutexSize > iDataSize - 10*1024*1024)
        {
            //所需空间太大一个内存块都不够放//预留10M空间
            CHECK_RET(-1,"DataSize is[%luM],it's too small,must > [%lldM],please change it",
                      iDataSize/1024/1024, iMutexSize/1024/1024);
        }
        bool bFind = false;
        TMdbHashMutexMgrInfo* pMutexMgr     = NULL;
        int i = 0;
        for(i = 0; i < MAX_SHM_ID; i++)
        {
            pMutexMgr = (TMdbHashMutexMgrInfo*)m_pMdbShmDsn->GetHashMutex(i);
            if(NULL ==  pMutexMgr ) //需要申请新的索引内存
            {
                CHECK_RET(CreateHashNewMutexShm(iDataSize),"CreateNewBIndexShm[%d]failed",i);
                pMutexMgr = (TMdbHashMutexMgrInfo*)m_pMdbShmDsn->GetHashMutex(i);
                CHECK_OBJ(pMutexMgr);
            }
            CHECK_RET(pMutexMgr->tMutex.Lock(true),"Lock Faild");
            do
            {
                int j = 0;
                TMdbHashBaseMutex* pMutex = NULL;
                //搜寻可以放置索引信息的位置
                for(j = 0; j<MAX_BASE_INDEX_COUNTS; j++)
                {
                    if('0' == pMutexMgr->aBaseMutex[j].cState)
                    {
                        //未创建的
                        pMutex = &(pMutexMgr->aBaseMutex[j]);
                        stTableIndexInfo.pBaseIndex->iMutexMgrPos= i;
                        stTableIndexInfo.pBaseIndex->iMutexPos= j;
                        stTableIndexInfo.iMutexPos= j;
                        break;
                    }
                }
                if(NULL == pMutex)
                {
                    break;   //没有空闲位置可以放索引信息
                }
                //搜寻是否还有空闲内存
                for(j = 0; j<MAX_BASE_INDEX_COUNTS; j++)
                {
                    if(pMutexMgr->tFreeSpace[j].iPosAdd >0)
                    {
                        TMDBIndexFreeSpace& tFreeSpace = pMutexMgr->tFreeSpace[j];
                        if(tFreeSpace.iSize >= (size_t)iMutexSize)
                        {
                            pMutex->cState = '2';//更改状态
                            pMutex->iPosAdd   = tFreeSpace.iPosAdd;
                            pMutex->iSize     = iMutexSize;
                            if(tFreeSpace.iSize - iMutexSize > 0)
                            {
                                //还有剩余空间
                                tFreeSpace.iPosAdd += iMutexSize;
                                tFreeSpace.iSize   -= iMutexSize;
								printf("Use Mutex size:%d,Left:%d,[%d][%d].\n",iMutexSize,tFreeSpace.iSize,i,j);
                            }
                            else
                            {
                                //该块空闲空间正好用光
                                tFreeSpace.Clear();
                            }
                            CHECK_RET_BREAK(DefragIndexSpace(pMutexMgr->tFreeSpace),"DefragIndexSpace failed...");
                        }
                    }
                }
                CHECK_RET_BREAK(iRet,"iRet = [%d]",iRet);
                if('2' != pMutex->cState)
                {
                    //没有空闲内存块放索引节点
                    break;
                }
                else
                {
                    //申请到空闲内存块
                    stTableIndexInfo.pMutex= pMutex;
                    stTableIndexInfo.pMutexMgr = pMutexMgr;
                    bFind = true;
                    break;
                }
            }
            while(0);
            CHECK_RET(pMutexMgr->tMutex.UnLock(true),"UnLock Faild");
            if(bFind)
            {
                break;   //找到
            }
        }
        if(!bFind)
        {
            CHECK_RET(-1,"GetMHashFreeMutexShm failed....");
        }
        TADD_FUNC("Finish.");
        return iRet;
    }    /******************************************************************************
    * 函数名称	:  CreateNewBIndexShm
    * 函数描述	:  创建新的基础索引内存块
    * 输入		:  iShmSize - 内存块大小
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::CreateNewBIndexShm(size_t iShmSize)
    {
        TADD_FUNC("Start.Size=[%lu].",iShmSize);
        int iRet = 0;
        if(MAX_SHM_ID == m_pMdbShmDsn->GetInfo()->iBaseIndexShmCounts)
        {
            CHECK_RET(ERR_OS_NO_MEMROY,"can't create new shm,MAX_SHM_COUNTS[%d]",MAX_SHM_ID);
        }
        int iPos = m_pMdbDsn->iBaseIndexShmCounts;
        TADD_FLOW("Create IndexShm:[%d],base_key[0x%0x]",iPos,m_pMdbDsn->iBaseIndexShmKey[iPos]);
        CHECK_RET(TMdbShm::Create(m_pMdbDsn->iBaseIndexShmKey[iPos], iShmSize, m_pMdbDsn->iBaseIndexShmID[iPos]),
                  " Can't Create BaseIndexShm errno=%d[%s].", errno, strerror(errno));
        TADD_FLOW("Base_SHM_ID =[%d].SHM_SIZE=[%lu].",m_pMdbDsn->iBaseIndexShmID[iPos],iShmSize);
        TADD_NORMAL("Create Base IndexShm:[%d],size=[%luMB]",iPos,iShmSize/(1024*1024));
        m_pMdbDsn->iBaseIndexShmCounts ++;
        CHECK_RET(m_pMdbShmDsn->ReAttachIndex(),"ReAttachIndex failed....");//对于新创建的shm获取新映射地址
        //初始化索引区信息
        TMdbBaseIndexMgrInfo* pBaseIndexMgr = (TMdbBaseIndexMgrInfo*)m_pMdbShmDsn->GetBaseIndex(iPos);// ->m_pBaseIndexShmAddr[iPos];
        CHECK_OBJ(pBaseIndexMgr);
        pBaseIndexMgr->iSeq = iPos;
        int i = 0;
        for(i = 0; i<MAX_BASE_INDEX_COUNTS; i++)
        {
            pBaseIndexMgr->tIndex[i].Clear();
            pBaseIndexMgr->tFreeSpace[i].Clear();
        }
        pBaseIndexMgr->tFreeSpace[0].iPosAdd = sizeof(TMdbBaseIndexMgrInfo);
        pBaseIndexMgr->tFreeSpace[0].iSize   = iShmSize - sizeof(TMdbBaseIndexMgrInfo);

        pBaseIndexMgr->iIndexCounts = 0;
        pBaseIndexMgr->iTotalSize   = iShmSize;
        pBaseIndexMgr->tMutex.Create();
        TADD_FUNC("Finish.");
        return iRet;
    }
    /******************************************************************************
    * 函数名称	:  CreateNewCIndexShm
    * 函数描述	:  创建新的冲突索引内存块
    * 输入		:  iShmSize - 内存块大小
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::CreateNewCIndexShm(size_t iShmSize)
    {
        TADD_FUNC("Start.size=[%lu].",iShmSize);
        int iRet = 0;
        if(MAX_SHM_ID == m_pMdbShmDsn->GetInfo()->iConflictIndexShmCounts)
        {
            CHECK_RET(ERR_OS_NO_MEMROY,"can't create new shm,MAX_SHM_COUNTS[%d]",MAX_SHM_ID);
        }
        int iPos = m_pMdbDsn->iConflictIndexShmCounts;//pos
        TADD_FLOW("Create IndexShm:[%d],conflict_key[0x%0x]",iPos,m_pMdbDsn->iConflictIndexShmKey[iPos]);
        CHECK_RET(TMdbShm::Create(m_pMdbDsn->iConflictIndexShmKey[iPos], iShmSize, m_pMdbDsn->iConflictIndexShmID[iPos]),
                  " Can't Create ConflictIndexShm, errno=%d[%s].", errno, strerror(errno));
        TADD_FLOW("Conflict_SHM_ID = [%d],SHM_SIZE=[%lu]",m_pMdbDsn->iConflictIndexShmID[iPos],iShmSize);
        TADD_NORMAL("Create Conflict IndexShm:[%d],size=[%luMB]",iPos,iShmSize/(1024*1024));
        m_pMdbDsn->iConflictIndexShmCounts ++;
        CHECK_RET(m_pMdbShmDsn->ReAttachIndex(),"ReAttachIndex failed....");//对于新创建的shm获取新映射地址
        //初始化索引区信息
        TMdbConflictIndexMgrInfo* pConflictIndexMgr = (TMdbConflictIndexMgrInfo*)m_pMdbShmDsn->GetConflictIndex(iPos);//m_pConflictIndexShmAddr[iPos];
        CHECK_OBJ(pConflictIndexMgr);
        pConflictIndexMgr->iSeq = iPos;
        int i = 0;
        for(i = 0; i<MAX_BASE_INDEX_COUNTS; i++)
        {
            pConflictIndexMgr->tIndex[i].Clear();
            pConflictIndexMgr->tFreeSpace[i].Clear();
        }
        pConflictIndexMgr->tFreeSpace[0].iPosAdd = sizeof(TMdbConflictIndexMgrInfo);
        pConflictIndexMgr->tFreeSpace[0].iSize   = iShmSize - sizeof(TMdbConflictIndexMgrInfo);
        //pConflictIndexMgr->iLeftAdd = sizeof(TMdbConflictIndexMgrInfo);
        pConflictIndexMgr->iIndexCounts = 0;
        pConflictIndexMgr->iTotalSize   = iShmSize;
        pConflictIndexMgr->tMutex.Create();
        TADD_FUNC("Finish.");
        return iRet;
    }
    /******************************************************************************
    * 函数名称	:  InitBCIndex
    * 函数描述	:  初始化基础/冲突索引
    * 输入		: pTable - 表信息
    * 输入		:
    * 输出		:   tTableIndex - 表所对应的索引信息
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::InitBCIndex(ST_HASH_INDEX_INFO & tTableIndex,TMdbTable * pTable)
    {
        int iRet = 0;
        //初始化基础索引
        SAFESTRCPY(tTableIndex.pBaseIndex->sTabName, sizeof(tTableIndex.pBaseIndex->sTabName), pTable->sTableName);
        
        tTableIndex.pBaseIndex->cState   = '1';
        TMdbDateTime::GetCurrentTimeStr(tTableIndex.pBaseIndex->sCreateTime);
        InitIndexNode((TMdbIndexNode *)((char *)tTableIndex.pBIndexMgr + tTableIndex.pBaseIndex->iPosAdd),
                      tTableIndex.pBaseIndex->iSize,false);
        tTableIndex.pBIndexMgr->iIndexCounts ++;
        //初始化冲突索引
        tTableIndex.pConflictIndex->cState = '1';
        TMdbDateTime::GetCurrentTimeStr(tTableIndex.pConflictIndex->sCreateTime);
        InitIndexNode((TMdbIndexNode *)((char *)tTableIndex.pCIndexMgr + tTableIndex.pConflictIndex->iPosAdd),
                      tTableIndex.pConflictIndex->iSize,true);
        tTableIndex.pConflictIndex->iFreeHeadPos = 0;//空闲结点位置
        tTableIndex.pConflictIndex->iFreeNodeCounts = tTableIndex.pConflictIndex->iSize/sizeof(TMdbIndexNode);//记录空闲冲突节点个数
        tTableIndex.pCIndexMgr->iIndexCounts ++;

        return iRet;
    }

	int TMdbHashIndexCtrl::InitBRePosCIndex(ST_HASH_INDEX_INFO & tTableIndex,TMdbTable * pTable)
	{
	    int iRet = 0;
	    //初始化基础索引
        SAFESTRCPY(tTableIndex.pBaseIndex->sTabName, sizeof(tTableIndex.pBaseIndex->sTabName), pTable->sTableName);
	    tTableIndex.pBaseIndex->cState   = '1';
	    TMdbDateTime::GetCurrentTimeStr(tTableIndex.pBaseIndex->sCreateTime);
	    InitIndexNode((TMdbIndexNode *)((char *)tTableIndex.pBIndexMgr + tTableIndex.pBaseIndex->iPosAdd),tTableIndex.pBaseIndex->iSize,false);
	    tTableIndex.pBIndexMgr->iIndexCounts ++;
	    //初始化冲突索引
	    tTableIndex.pConflictIndex->cState = '1';
	    TMdbDateTime::GetCurrentTimeStr(tTableIndex.pConflictIndex->sCreateTime);
	    InitRePosIndexNode((TMdbReIndexNode *)((char *)tTableIndex.pCIndexMgr + tTableIndex.pConflictIndex->iPosAdd),
	                  tTableIndex.pConflictIndex->iSize,true);
	    tTableIndex.pConflictIndex->iFreeHeadPos = 0;//空闲结点位置
	    tTableIndex.pConflictIndex->iFreeNodeCounts = tTableIndex.pConflictIndex->iSize/sizeof(TMdbReIndexNode);//记录空闲冲突节点个数
	    tTableIndex.pCIndexMgr->iIndexCounts ++;

	    return iRet;
	}

    /******************************************************************************
    * 函数名称	:  GetFreeBIndexShm
    * 函数描述	:  获取空闲基础索引块，只做获取不做任何修改
    * 输入		:  iBaseIndexSize -- 基础索引大小
    * 输入		:  iDataSize  --  一个共享内存块大小
    * 输出		:  stTableIndexInfo  -- 获取到的索引信息
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::GetFreeBIndexShm(MDB_INT64 iBaseIndexSize,size_t iDataSize,
                                        ST_HASH_INDEX_INFO & stTableIndexInfo)
    {
        TADD_FUNC("Start.iBaseIndexSize=[%lld].iDataSize=[%lu]",iBaseIndexSize,iDataSize);
        int iRet = 0;
        if((size_t)iBaseIndexSize > iDataSize - 10*1024*1024)
        {
            //所需空间太大一个内存块都不够放//预留10M空间
            CHECK_RET(-1,"DataSize is[%luM],it's too small,must > [%lldM],please change it",
                      iDataSize/1024/1024, iBaseIndexSize/1024/1024);
        }
        bool bFind = false;
        TMdbBaseIndexMgrInfo* pBIndexMgr     = NULL;
        int i = 0;
        for(i = 0; i < MAX_SHM_ID; i++)
        {
            pBIndexMgr = (TMdbBaseIndexMgrInfo*)m_pMdbShmDsn->GetBaseIndex(i);
            if(NULL ==  pBIndexMgr ) //需要申请新的索引内存
            {
                CHECK_RET(CreateNewBIndexShm(iDataSize),"CreateNewBIndexShm[%d]failed",i);
                pBIndexMgr = (TMdbBaseIndexMgrInfo*)m_pMdbShmDsn->GetBaseIndex(i);
                CHECK_OBJ(pBIndexMgr);
            }
            CHECK_RET(pBIndexMgr->tMutex.Lock(true),"Lock failed.");
            do
            {
                int j = 0;
                TMdbBaseIndex * pBaseIndex = NULL;
                //搜寻可以放置索引信息的位置
                for(j = 0; j<MAX_BASE_INDEX_COUNTS; j++)
                {
                    if('0' == pBIndexMgr->tIndex[j].cState)
                    {
                        //未创建的
                        pBaseIndex = &(pBIndexMgr->tIndex[j]);
                        stTableIndexInfo.iBaseIndexPos = j;
                        break;
                    }
                }
                if(NULL == pBaseIndex)
                {
                    break;   //没有空闲位置可以放索引信息
                }
                //搜寻是否还有空闲内存
                for(j = 0; j<MAX_BASE_INDEX_COUNTS; j++)
                {
                    if(pBIndexMgr->tFreeSpace[j].iPosAdd >0)
                    {
                        TMDBIndexFreeSpace& tFreeSpace = pBIndexMgr->tFreeSpace[j];
                        if(tFreeSpace.iSize >= (size_t)iBaseIndexSize)
                        {
                            pBaseIndex->cState = '2';//更改状态
                            pBaseIndex->iPosAdd   = tFreeSpace.iPosAdd;
                            pBaseIndex->iSize     = iBaseIndexSize;
                            if(tFreeSpace.iSize - iBaseIndexSize > 0)
                            {
                                //还有剩余空间
                                tFreeSpace.iPosAdd += iBaseIndexSize;
                                tFreeSpace.iSize   -= iBaseIndexSize;
                            }
                            else
                            {
                                //该块空闲空间正好用光
                                tFreeSpace.Clear();
                            }
                            CHECK_RET_BREAK(DefragIndexSpace(pBIndexMgr->tFreeSpace),"DefragIndexSpace failed...");
                        }
                    }
                }
                CHECK_RET_BREAK(iRet,"iRet = [%d]",iRet);
                if('2' != pBaseIndex->cState)
                {
                    //没有空闲内存块放索引节点
                    break;
                }
                else
                {
                    //申请到空闲内存块
                    stTableIndexInfo.pBaseIndex = pBaseIndex;
                    stTableIndexInfo.pBIndexMgr = pBIndexMgr;
                    bFind = true;
                    break;
                }
            }
            while(0);
            CHECK_RET(pBIndexMgr->tMutex.UnLock(true),"unlock failed.");
            if(bFind)
            {
                break;   //找到
            }
        }
        if(!bFind)
        {
            CHECK_RET(-1,"GetFreeBIndexShm failed....");
        }
        TADD_FUNC("Finish.");
        return iRet;
    }
    /******************************************************************************
    * 函数名称	:  GetFreeCIndexShm
    * 函数描述	:  获取空闲冲突索引内存块，只做获取不做任何修改
    * 输入		:  iConflictIndexSize -- 冲突索引大小
    * 输入		:  iDataSize  --  一个共享内存块大小
    * 输出		:  stTableIndexInfo  -- 获取到的索引信息
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::GetFreeCIndexShm(MDB_INT64 iConflictIndexSize,size_t iDataSize,
                                        ST_HASH_INDEX_INFO & stTableIndexInfo, bool bRePosIndex)
    {
        TADD_FUNC("Start.iConflictIndexSize=[%lld].iDataSize=[%lld]",iConflictIndexSize,iDataSize);
        int iRet = 0;
        if((size_t)iConflictIndexSize > iDataSize - 10*1024*1024)
        {
            //所需空间太大一个内存块都不够放//预留10M空间
            CHECK_RET(-1,"DataSize is[%luM],it's too small,must > [%lldM],please change it",
                      iDataSize/1024/1024, iConflictIndexSize/1024/1024);
        }
        bool bFind = false;
        TMdbConflictIndexMgrInfo* pCIndexMgr     = NULL;
        int i = 0;
        for(i = 0; i < MAX_SHM_ID; i++)
        {
            pCIndexMgr = (TMdbConflictIndexMgrInfo*)m_pMdbShmDsn->GetConflictIndex(i);
            if(NULL ==  pCIndexMgr ) //需要申请新的索引内存
            {
                CHECK_RET(CreateNewCIndexShm(iDataSize),"CreateNewCIndexShm[%d]failed",i);
                pCIndexMgr = (TMdbConflictIndexMgrInfo*)m_pMdbShmDsn->GetConflictIndex(i);
                CHECK_OBJ(pCIndexMgr);
            }
            CHECK_RET(pCIndexMgr->tMutex.Lock(true),"lock failed.");
            do
            {
                int j = 0;
                TMdbConflictIndex * pConflictIndex = NULL;
                //搜寻可以放置索引信息的位置
                for(j = 0; j<MAX_BASE_INDEX_COUNTS; j++)
                {
                    if('0' == pCIndexMgr->tIndex[j].cState)
                    {
                        //未创建的
                        pConflictIndex = &(pCIndexMgr->tIndex[j]);
                        stTableIndexInfo.pBaseIndex->iConflictMgrPos   = i;
                        stTableIndexInfo.pBaseIndex->iConflictIndexPos = j;
                        stTableIndexInfo.iConflictIndexPos = j;
                        break;
                    }
                }
                if(NULL == pConflictIndex)
                {
                    break;   //没有空闲位置可以放索引信息
                }
                //搜寻是否还有空闲内存
                for(j = 0; j<MAX_BASE_INDEX_COUNTS; j++)
                {
                    if(pCIndexMgr->tFreeSpace[j].iPosAdd >0)
                    {
                        TMDBIndexFreeSpace & tFreeSpace = pCIndexMgr->tFreeSpace[j];
                        if(tFreeSpace.iSize >=(size_t) iConflictIndexSize)
                        {
							pConflictIndex->bRePos = bRePosIndex;
                            pConflictIndex->cState = '2';//更改状态
                            pConflictIndex->iPosAdd   = tFreeSpace.iPosAdd;
                            pConflictIndex->iSize     = iConflictIndexSize;
                            if(tFreeSpace.iSize - iConflictIndexSize > 0)
                            {
                                //还有剩余空间
                                tFreeSpace.iPosAdd += iConflictIndexSize;
                                tFreeSpace.iSize   -= iConflictIndexSize;
                            }
                            else
                            {
                                //该块空闲空间正好用光
                                tFreeSpace.Clear();
                            }
                            CHECK_RET_BREAK(DefragIndexSpace(pCIndexMgr->tFreeSpace),"DefragIndexSpace failed...");
                        }
                    }
                }
                CHECK_RET_BREAK(iRet,"iRet = [%d].",iRet);
                if('2' != pConflictIndex->cState)
                {
                    //没有空闲内存块放索引节点
                    break;
                }
                else
                {
                    //申请到空闲内存块
                    stTableIndexInfo.pConflictIndex = pConflictIndex;
                    stTableIndexInfo.pCIndexMgr     = pCIndexMgr;
                    bFind = true;
                    break;
                }
            }while(0);
            CHECK_RET(pCIndexMgr->tMutex.UnLock(true),"unlock failed.");
            if(bFind)
            {
                break;   //找到
            }
        }
        if(!bFind)
        {
            CHECK_RET(-1,"GetFreeCIndexShm failed....");
        }
        TADD_FUNC("Finish.");
        return iRet;
    }

 int TMdbHashIndexCtrl::CalcBaseMutexCount(int iBaseCont)
    {
		int iRet = 1;// at least 1
				
		if(iBaseCont < 9973)
		{
			iRet = iBaseCont;
		}
		else if(iBaseCont>= 9973 && iBaseCont < 99991)
		{

			iRet = 9973;
		}
		else if(iBaseCont >= 99991)
		{
			iRet = 99991;
		}
		
		return iRet;

    }

	//冲突索引，每次申请的数量
	int TMdbHashIndexCtrl::GetCApplyNum(int iBaseCont)
	{
		return iBaseCont/100;
		
	}

    int TMdbHashIndexCtrl::AddTableSingleIndex(TMdbTable * pTable,int iIndexPos, size_t iDataSize)
    {
        int iRet = 0;
        CHECK_OBJ(pTable);
        TADD_FLOW("AddTableIndex Table=[%s],Size=[%lu] start.", pTable->sTableName,iDataSize);
        MDB_INT64 iBaseIndexSize     = pTable->iRecordCounts * sizeof(TMdbIndexNode); //计算单个基础索引需要的空间
        MDB_INT64 iConflictIndexSize = pTable->iRecordCounts * sizeof(TMdbIndexNode); //计算单个冲突索引需要的空间
        
		int iMutexCount = CalcBaseMutexCount(pTable->iTabLevelCnts);
		MDB_INT64 iMutexSize = iMutexCount * sizeof(TMutex);
		ST_HASH_INDEX_INFO stTableIndex;
        stTableIndex.Clear();
		
        CHECK_RET(GetFreeBIndexShm(iBaseIndexSize, iDataSize,stTableIndex),"GetFreeBIndexShm failed..");
        CHECK_RET(GetHashFreeMutexShm(iMutexSize, iDataSize,stTableIndex),"GetFreeBIndexShm failed..");
		CHECK_RET(InitHashMutex(stTableIndex,pTable),"InitHashMutex failed...");
		SAFESTRCPY(stTableIndex.pMutex->sName, sizeof(stTableIndex.pMutex->sName), pTable->tIndex[iIndexPos].sName);
		
	    if(pTable->tIndex[iIndexPos].IsRedirectIndex())
	    {
	        iConflictIndexSize = pTable->iRecordCounts * sizeof(TMdbReIndexNode);
	        CHECK_RET(GetFreeCIndexShm(iConflictIndexSize, iDataSize,stTableIndex,true),"GetFreeCIndexShm failed...");
	        if('2' == stTableIndex.pBaseIndex->cState)
	        {
	            //找到空闲位置
	            SAFESTRCPY(stTableIndex.pBaseIndex->sName,sizeof(stTableIndex.pBaseIndex->sName),pTable->tIndex[iIndexPos].sName);
				CHECK_RET(InitBRePosCIndex(stTableIndex,pTable),"InitBRePosCIndex failed...");
	        }
	        else
	        {
	            CHECK_RET(-1,"not find pos for new index....");
	        }
	    }
	    else
	    {
	        iConflictIndexSize = pTable->iRecordCounts * sizeof(TMdbIndexNode);
	        CHECK_RET(GetFreeCIndexShm(iConflictIndexSize,iDataSize,stTableIndex,false),"GetFreeCIndexShm failed...");
	        if('2' == stTableIndex.pBaseIndex->cState)
	        {
	            //找到空闲位置
	            SAFESTRCPY(stTableIndex.pBaseIndex->sName,sizeof(stTableIndex.pBaseIndex->sName),pTable->tIndex[iIndexPos].sName);
				CHECK_RET(InitBCIndex(stTableIndex,pTable),"InitBCIndex failed...");
	        }
	        else
	        {
	            CHECK_RET(-1,"not find pos for new index....");
	        }
	    }
		
        TADD_FLOW("Index[%s]",pTable->tIndex[iIndexPos].sName);
        TADD_FLOW("AddTableIndex : Table=[%s] finish.", pTable->sTableName);
        return iRet;
    }


  int TMdbHashIndexCtrl::InitHashMutex(ST_HASH_INDEX_INFO & tTableIndex,TMdbTable * pTable)
    {
        TADD_FUNC("Start.");
        int iRet = 0;
        //初始化基础索引
        SAFESTRCPY(tTableIndex.pMutex->sTabName, sizeof(tTableIndex.pMutex->sTabName), pTable->sTableName);
        tTableIndex.pMutex->cState   = '1';
        tTableIndex.pMutex->iMutexCnt = tTableIndex.pMutex->iSize/sizeof(TMiniMutex);
        TMdbDateTime::GetCurrentTimeStr(tTableIndex.pMutex->sCreateTime);
        InitMutexNode((TMiniMutex*)((char *)tTableIndex.pMutexMgr+ tTableIndex.pMutex->iPosAdd),
                      tTableIndex.pMutex->iSize);
        tTableIndex.pMutexMgr->iCounts++;
        TADD_FUNC("Finish.");
        return iRet;
    }
	int TMdbHashIndexCtrl::InitMutexNode(TMiniMutex* pNode,MDB_INT64 iSize)
    {
        MDB_INT64 iCount = iSize/sizeof(TMiniMutex);
        for(MDB_INT64 n=0; n<iCount; ++n)
        {
            pNode->Create();
            ++pNode;
        }
        return 0;
    }

    /******************************************************************************
    * 函数名称	:  GetTableByIndexName
    * 函数描述	:  根据索引名获取表
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::GetTableByIndexName(const char * sIndexName,TMdbTable * & pRetTable)
    {
        TADD_FUNC("Start.sIndexName=[%s]",sIndexName);
        int iRet = 0;
        pRetTable = NULL;
        CHECK_OBJ(m_pMdbShmDsn);
        int j = 0;
        TShmList<TMdbTable>::iterator itor = m_pMdbShmDsn->m_TableList.begin();
        for(;itor != m_pMdbShmDsn->m_TableList.end();++itor)
        {
            TMdbTable * pTable = &(*itor);
            if(pTable->sTableName[0] == 0){continue;}
            // 遍历表，找出索引名所对应的表
            for(j = 0; j<pTable->iIndexCounts; j++)
            {
                if(TMdbNtcStrFunc::StrNoCaseCmp(pTable->tIndex[j].sName,sIndexName) == 0)
                {
                    pRetTable = pTable;
                    return iRet;
                }
            }
        }
        TADD_FUNC("Finish.pRetTable=[%p].",pRetTable);
        return iRet;
    }

    /******************************************************************************
    * 函数名称	:  DeleteTableIndex
    * 函数描述	:  删除某个表的索引
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::DeleteTableIndex(ST_HASH_INDEX_INFO& tIndexInfo)
    {
        int iRet = 0;

        //清理基础索引信息
        CHECK_RET(tIndexInfo.pBIndexMgr->tMutex.Lock(true),"lock failed.");
        do
        {
            CHECK_RET_BREAK(RecycleIndexSpace(tIndexInfo.pBIndexMgr->tFreeSpace,
                                              tIndexInfo.pBaseIndex->iPosAdd,
                                              tIndexInfo.pBaseIndex->iSize),"RecycleIndexSpace failed...");
            tIndexInfo.pBaseIndex->Clear();
            tIndexInfo.pBIndexMgr->iIndexCounts --;//基础索引-1
        }
        while(0);
        CHECK_RET(tIndexInfo.pBIndexMgr->tMutex.UnLock(true),"unlock failed.");
        CHECK_RET(iRet,"ERROR.");
        //清理冲突索引信息
        CHECK_RET(tIndexInfo.pCIndexMgr->tMutex.Lock(true),"lock failed.");
        do
        {
            CHECK_RET_BREAK(RecycleIndexSpace(tIndexInfo.pCIndexMgr->tFreeSpace,
                                              tIndexInfo.pConflictIndex->iPosAdd,
                                              tIndexInfo.pConflictIndex->iSize),"RecycleIndexSpace failed...");
            tIndexInfo.pConflictIndex->Clear();
            tIndexInfo.pCIndexMgr->iIndexCounts --;//冲突索引-1
        }
        while(0);
        CHECK_RET(tIndexInfo.pCIndexMgr->tMutex.UnLock(true),"unlock failed.");
        
        TADD_FUNC("Finish.");
        return iRet;
    }

	/******************************************************************************
    * 函数名称	:  TruncateTableIndex
    * 函数描述	:  删除某个表的索引
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::TruncateTableIndex(ST_HASH_INDEX_INFO& tIndexInfo)
    {
        int iRet = 0;
		//初始化基础索引
		InitIndexNode((TMdbIndexNode *)((char *)tIndexInfo.pBIndexMgr + tIndexInfo.pBaseIndex->iPosAdd),
					  tIndexInfo.pBaseIndex->iSize,false);

		if(NULL != tIndexInfo.pReConfNode)
		{
		
			//初始化冲突索引
			InitRePosIndexNode((TMdbReIndexNode *)((char *)tIndexInfo.pCIndexMgr +tIndexInfo.pConflictIndex->iPosAdd),
						  tIndexInfo.pConflictIndex->iSize,true);
			tIndexInfo.pConflictIndex->iFreeHeadPos = 0;//空闲结点位置
			tIndexInfo.pConflictIndex->iFreeNodeCounts = tIndexInfo.pConflictIndex->iSize/sizeof(TMdbReIndexNode);//记录空闲冲突节点个数
		}
		else
		{
			InitIndexNode((TMdbIndexNode *)((char *)tIndexInfo.pCIndexMgr + tIndexInfo.pConflictIndex->iPosAdd),
						  tIndexInfo.pConflictIndex->iSize,true);
			tIndexInfo.pConflictIndex->iFreeHeadPos = 0;//空闲结点位置
			tIndexInfo.pConflictIndex->iFreeNodeCounts = tIndexInfo.pConflictIndex->iSize/sizeof(TMdbIndexNode);//记录空闲冲突节点个数
		}
        TADD_FUNC("Finish.");
        return iRet;
    }

    /*int TMdbHashIndexCtrl::DeleteTableSpecifiedIndex(TMdbShmDSN * pMdbShmDsn,TMdbTable * pTable,const char* pIdxName)
    {
        int iRet = 0;
        CleanTableIndexInfo();
        CHECK_OBJ(pMdbShmDsn);
        CHECK_OBJ(pTable);
        TADD_FUNC("Start.table=[%s].",pTable->sTableName);
        CHECK_OBJ(pIdxName);
        CHECK_RET(AttachDsn(pMdbShmDsn),"Attach dsn failed...");
        //先Attach上表获取索引信息
        CHECK_RET(AttachTable(pMdbShmDsn,pTable),"Attach table[%s] failed....",pTable->sTableName);
        for(int i = 0; i<pTable->iIndexCounts; i++)
        {
            if(TMdbNtcStrFunc::StrNoCaseCmp(pTable->tIndex[i].sName,pIdxName) != 0)
            {
                continue;
            }
            if(m_arrTableIndex[i].bInit)
            {
                //清理基础索引信息
                m_arrTableIndex[i].pBIndexMgr->tMutex.Lock(true);
                do
                {
                    CHECK_RET_BREAK(RecycleIndexSpace(m_arrTableIndex[i].pBIndexMgr->tFreeSpace,
                                                      m_arrTableIndex[i].pBaseIndex->iPosAdd,
                                                      m_arrTableIndex[i].pBaseIndex->iSize),"RecycleIndexSpace failed...");
                    m_arrTableIndex[i].pBaseIndex->Clear();
                    m_arrTableIndex[i].pBIndexMgr->iIndexCounts --;//基础索引-1
                }
                while(0);
                m_arrTableIndex[i].pBIndexMgr->tMutex.UnLock(true);
                CHECK_RET(iRet,"ERROR.");
                //清理冲突索引信息
                m_arrTableIndex[i].pCIndexMgr->tMutex.Lock(true);
                do
                {
                    CHECK_RET_BREAK(RecycleIndexSpace(m_arrTableIndex[i].pCIndexMgr->tFreeSpace,
                                                      m_arrTableIndex[i].pConflictIndex->iPosAdd,
                                                      m_arrTableIndex[i].pConflictIndex->iSize),"RecycleIndexSpace failed...");
                    m_arrTableIndex[i].pConflictIndex->Clear();
                    m_arrTableIndex[i].pCIndexMgr->iIndexCounts --;//冲突索引-1
                }
                while(0);
                m_arrTableIndex[i].pCIndexMgr->tMutex.UnLock(true);
                CHECK_RET(iRet,"ERROR.");
            }
        }
        TADD_FUNC("Finish.");
        return iRet;
    }*/

    /******************************************************************************
    * 函数名称	:  RecycleIndexSpace
    * 函数描述	:  回收索引空间
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::RecycleIndexSpace(TMDBIndexFreeSpace tFreeSpace[],size_t iPosAdd,size_t iSize)
    {
        TADD_FUNC("Start.tFreeSpace=[%p],iPosAdd=[%d],iSize=[%d].",tFreeSpace,iPosAdd,iSize);
        int iRet = 0;
        int i = 0;
        for(i = 0; i<MAX_BASE_INDEX_COUNTS; i++)
        {
            if(tFreeSpace[i].iPosAdd == 0)
            {
                //找到一个空闲记录点记录下空闲区域
                tFreeSpace[i].iPosAdd = iPosAdd;
                tFreeSpace[i].iSize   = iSize;
                break;
            }
        }
        CHECK_RET(DefragIndexSpace(tFreeSpace),"DefragIndexSpace failed....");
        TADD_FUNC("Finish.");
        return iRet;
    }
    /******************************************************************************
    * 函数名称	:  PrintIndexSpace
    * 函数描述	: 打印索引空间信息
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::PrintIndexSpace(const char * sPreInfo,TMDBIndexFreeSpace tFreeSpace[])
    {
        printf("\n%s:",sPreInfo);
        for(int i = 0; i<MAX_BASE_INDEX_COUNTS; i++)
        {
            if(tFreeSpace[i].iPosAdd == 0)
            {
                break;   //结束
            }
            printf("\n(%d)PosAdd=[%d],Size[%d] ",i,(int)tFreeSpace[i].iPosAdd,(int)tFreeSpace[i].iSize);
        }
        return 0;
    }
    /******************************************************************************
    * 函数名称	:  DefragIndexSpace
    * 函数描述	:  整理索引空间
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    int TMdbHashIndexCtrl::DefragIndexSpace(TMDBIndexFreeSpace tFreeSpace[])
    {
        TADD_FUNC("Start.");
        int iRet = 0;
        int i,j;
        //先按从小到大排序，并且将无用的节点(iposAdd < 0)排到后面
        TMDBIndexFreeSpace tTempSapce;
        for(i = 0; i<MAX_BASE_INDEX_COUNTS; i++)
        {
            for(j = i; j<MAX_BASE_INDEX_COUNTS; j++)
            {
                if((tFreeSpace[i].iPosAdd > tFreeSpace[j].iPosAdd && tFreeSpace[j].iPosAdd>0)
                        || tFreeSpace[i].iPosAdd == 0)
                {
                    tTempSapce    = tFreeSpace[i];
                    tFreeSpace[i] = tFreeSpace[j];
                    tFreeSpace[j] = tTempSapce;
                }
            }
        }
        //查看相邻的节点是否可以合并
        for(i = 0; i<MAX_BASE_INDEX_COUNTS - 1;)
        {
            if(tFreeSpace[i].iPosAdd == 0)
            {
                break;   //结束
            }
            if(tFreeSpace[i].iPosAdd + tFreeSpace[i].iSize == tFreeSpace[i+1].iPosAdd )
            {
                //可以合并，并停留在这个节点
                TADD_DETAIL("FreeSpace[%d] and [%d] can merge.",i,i+1);
                tFreeSpace[i].iSize += tFreeSpace[i+1].iSize;
                if(i+2 == MAX_BASE_INDEX_COUNTS)
                {
                    tFreeSpace[i+1].Clear();
                }
                else
                {
                    //将后来的节点前移
                    memmove(&(tFreeSpace[i+1]),&(tFreeSpace[i+2]),
                            sizeof(TMDBIndexFreeSpace)*(MAX_BASE_INDEX_COUNTS-i-2));
                    tFreeSpace[MAX_BASE_INDEX_COUNTS - 1].Clear();
                }
            }
            else if(tFreeSpace[i].iPosAdd + tFreeSpace[i].iSize > tFreeSpace[i+1].iPosAdd &&
                    tFreeSpace[i+1].iPosAdd > 0 )
            {
                //说明地址有重叠报错
                CHECK_RET(-1,"address is overlaped....");
            }
            else
            {
                i++;//判断下一个节点
            }
        }
        TADD_FUNC("Finish.");
        return iRet;
    }
    /******************************************************************************
    * 函数名称	:  GetIndexByColumnPos
    * 函数描述	:  根据columnpos获取indexnode
    * 输入		:  iColumnPos - 列位置
    * 输入		:  iColNoPos-这个索引组中的位置
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    /*ST_TABLE_INDEX_INFO * TMdbHashIndexCtrl::GetIndexByColumnPos(int iColumnPos,int &iColNoPos)
    {
        TADD_FUNC("Start.iColumnPos=[%d].",iColumnPos);
        int i = 0;
        for(i = 0; i < m_pAttachTable->iIndexCounts; i++)
        {
            if(m_arrTableIndex[i].bInit)
            {
                TMdbIndex * pMdbIndex = m_arrTableIndex[i].pIndexInfo;
                int j = 0;
                for(j = 0; j<MAX_INDEX_COLUMN_COUNTS; j++)
                {
                    if(iColumnPos == pMdbIndex->iColumnNo[j])
                    {
                        iColNoPos = j;
                        TADD_FUNC("Finish.iColNoPos=[%d]",iColNoPos);
                        return &m_arrTableIndex[i];
                    }
                }
            }
        }
        return NULL;
    }*/

    /******************************************************************************
    * 函数名称	:  RemoveDupIndexColumn
    * 函数描述	:  清除重复的可能索引列
    * 输入		:  
    * 输入		:  
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    /*int TMdbHashIndexCtrl::RemoveDupIndexColumn(std::vector<ST_INDEX_COLUMN> & vLeftIndexColumn,
                            std::vector<ST_INDEX_COLUMN> & vRightIndexColumn)
    {
         //去除重复的可能索引列
        std::vector<ST_INDEX_COLUMN>::iterator itorLeft = vLeftIndexColumn.begin();
        for(;itorLeft != vLeftIndexColumn.end(); ++itorLeft)
        {
            std::vector<ST_INDEX_COLUMN>::iterator itorRight = vRightIndexColumn.begin();
            for(;itorRight != vRightIndexColumn.end();)
            {
                if(itorLeft->pColumn == itorRight->pColumn)
                {//有列相同
                   itorRight =  vRightIndexColumn.erase(itorRight);
                }
                else
                {
                    ++ itorRight;
                }
            }
        }
        return 0;
    }*/
    /******************************************************************************
    * 函数名称	:  RemoveDupIndexValue
    * 函数描述	:  清除重复的索引列
    * 输入		:  
    * 输入		:  
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    /*int TMdbHashIndexCtrl::RemoveDupIndexValue(std::vector<ST_INDEX_VALUE> & vLeftIndexValue,std::vector<ST_INDEX_VALUE> & vRightIndexValue)
    {
        std::vector<ST_INDEX_VALUE>::iterator itorLeft = vLeftIndexValue.begin();
        for(;itorLeft != vLeftIndexValue.end();++itorLeft)
        {
             std::vector<ST_INDEX_VALUE>::iterator itorRight = vRightIndexValue.begin();
             for(;itorRight != vRightIndexValue.end();)
             {
                if(itorLeft->pstTableIndex == itorRight->pstTableIndex)
                {//有列相同
                   itorRight =  vRightIndexValue.erase(itorRight);
                }
                else
                {
                    ++ itorRight;
                }
            }
        }
        return 0;
    }*/


    /******************************************************************************
    * 函数名称	:  GenerateIndexValue
    * 函数描述	:  生成索引值组合
    * 输入		:  
    * 输入		:  
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    /*int TMdbHashIndexCtrl::GenerateIndexValue(ST_INDEX_COLUMN *pIndexColumnArr [] ,ST_TABLE_INDEX_INFO  * pstTableIndexInfo,
        int iCurPos,std::vector<ST_INDEX_VALUE> & vIndexValue)
    {
        int iRet = 0;
        TADD_FUNC("iCurPos = [%d],vIndexValue.size=[%d]",iCurPos,vIndexValue.size());
        if(pstTableIndexInfo->pIndexInfo->iColumnNo[iCurPos] < 0 || iCurPos >= MAX_INDEX_COLUMN_COUNTS){return iRet;}//索引列遍历完毕
        int iColPos = pstTableIndexInfo->pIndexInfo->iColumnNo[iCurPos];
        ST_INDEX_COLUMN * pstIndexColumn = pIndexColumnArr[iColPos];
        CHECK_OBJ(pstIndexColumn);
        if(0 == iCurPos)
        {//首次填充
            if(0 != vIndexValue.size()){CHECK_RET(ERR_APP_INVALID_PARAM,"vIndexValue should be 0,but size=[%d]",vIndexValue.size());}
            unsigned int i = 0;
            for(i = 0; i< pstIndexColumn->vExpr.size();++i)
            {
                ST_INDEX_VALUE tIndexValue;
                tIndexValue.pstTableIndex = pstTableIndexInfo;
                tIndexValue.pExprArr[iCurPos] = pstIndexColumn->vExpr[i];
                vIndexValue.push_back(tIndexValue);
            }
        }
        else
        {
            std::vector<ST_INDEX_VALUE> vTemp;
            vTemp.assign(vIndexValue.begin(),vIndexValue.end());
            vIndexValue.clear();
            unsigned int i = 0;
            for(i = 0;i < vTemp.size();++i)
            {
                unsigned int j = 0;
                for(j = 0; j < pstIndexColumn->vExpr.size();++ j)
                {//组合
                    ST_INDEX_VALUE & tIndexValue  =  vTemp[i];
                    tIndexValue.pstTableIndex = pstTableIndexInfo;
                    tIndexValue.pExprArr[iCurPos] = pstIndexColumn->vExpr[j];
                    vIndexValue.push_back(tIndexValue);
                }
            }
        }
        pIndexColumnArr[iColPos] = NULL;//标识已经处理过。
        return GenerateIndexValue(pIndexColumnArr,pstTableIndexInfo,iCurPos+1,vIndexValue);
    }*/

    /******************************************************************************
    * 函数名称	:  GetIndexByIndexColumn
    * 函数描述	:  根据可能的索引列获取索引
    * 输入		:  vIndexColumn - 可能的索引列
    * 输入		:  vIndexValue-获取到的索引
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/

    /*int TMdbHashIndexCtrl::GetIndexByIndexColumn(std::vector<ST_INDEX_COLUMN> & vIndexColumn,std::vector<ST_INDEX_VALUE> & vIndexValue)
    {
        TADD_FUNC("Start.vIndexColumn.size=[%d],vIndexValue.size[%d]",vIndexColumn.size(),vIndexValue.size());
        int iRet = 0;
        ST_INDEX_COLUMN * pIndexColumnArr[MAX_COLUMN_COUNTS] = {0};
        int i = 0;
        for(i = 0;i< vIndexColumn.size();++i)
        {
            pIndexColumnArr[vIndexColumn[i].pColumn->iPos] = &(vIndexColumn[i]);
        }
        for(i = 0; i < m_pAttachTable->iIndexCounts; i++)
        {
            if(m_arrTableIndex[i].bInit)
            {
                TMdbIndex * pMdbIndex = m_arrTableIndex[i].pIndexInfo;
                int j = 0;
                bool bFind = false;
                for(j = 0; j<MAX_INDEX_COLUMN_COUNTS; j++)
                {
                    if(pMdbIndex->iColumnNo[j] < 0){break;}//该索引检测完毕
                    if(NULL == pIndexColumnArr[pMdbIndex->iColumnNo[j]])
                    {
                        bFind = false;
                        break;
                    }
                    else
                    {
                        bFind = true;
                    }
                }
                if(bFind)
                {//可以使用该索引
                    std::vector<ST_INDEX_VALUE> vTemp;
                    CHECK_RET(GenerateIndexValue(pIndexColumnArr,&(m_arrTableIndex[i]),0,vTemp),"GenerateIndexValue failed.");
                    vIndexValue.insert(vIndexValue.end(),vTemp.begin(),vTemp.end());
                    break;//找到就可以退出
                }
            }
        }
        vIndexColumn.clear();//处理完清除
        TADD_FUNC("Start.vIndexColumn.size=[%d],vIndexValue.size[%d]",vIndexColumn.size(),vIndexValue.size());
        return iRet;
    }*/

    /******************************************************************************
    * 函数名称	:  GetScanAllIndex
    * 函数描述	:  获取全量遍历的索引
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    /*ST_TABLE_INDEX_INFO * TMdbHashIndexCtrl::GetScanAllIndex()
    {
        if(m_arrTableIndex[0].bInit)
        {
            return &(m_arrTableIndex[0]);
        }
        else
        {
            return NULL;
        }
    }*/

    /******************************************************************************
    * 函数名称	:  GetVerfiyPKIndex
    * 函数描述	:  获取校验主键的索引
    * 输入		:
    * 输入		:
    * 输出		:
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    /*ST_TABLE_INDEX_INFO * TMdbHashIndexCtrl::GetVerfiyPKIndex()
    {
        TADD_FUNC("Start.");
        if(0 == m_pAttachTable->m_tPriKey.iColumnCounts){return NULL;}
        int arrMatch[MAX_INDEX_COUNTS] = {0};//记录各个索引与主键的匹配度
        int i = 0;
        for(i = 0; i < m_pAttachTable->iIndexCounts;++i)
        {
            if(m_arrTableIndex[i].bInit)
            {
                TMdbIndex * pMdbIndex = m_arrTableIndex[i].pIndexInfo; 
                int j = 0;
                for(j = 0;j < m_pAttachTable->m_tPriKey.iColumnCounts;++j)
                {
                    int k = 0;
                    for(k = 0;k < MAX_INDEX_COLUMN_COUNTS;++k)
                    {
                        if(m_pAttachTable->m_tPriKey.iColumnNo[j] == pMdbIndex->iColumnNo[k])
                        {//有一样的列
                            arrMatch[i]++;
                            break;
                        }
                    }
                }
            }
        }
        //挑选匹配度最高的索引,并且是部分或全部主键列，不能比主键列多
        int iPos = 0;
        for(i = 0;i < m_pAttachTable->iIndexCounts;++i)
        {
            TMdbIndex * pMdbIndex = m_arrTableIndex[i].pIndexInfo; 
            int j = 0;
            bool bFindSame = false;
            for(j = 0;j < MAX_INDEX_COLUMN_COUNTS;++j)
            {
                if(pMdbIndex->iColumnNo[j] < 0){continue;}
                int k = 0;
                bFindSame = false;
                for(k = 0;k < m_pAttachTable->m_tPriKey.iColumnCounts;++k)
                {
                    if(pMdbIndex->iColumnNo[j] >=0 && m_pAttachTable->m_tPriKey.iColumnNo[k] == pMdbIndex->iColumnNo[j])
                    {//有不一样的列
                        bFindSame = true;
                        break;
                    }
                }
                if(false == bFindSame){break;}
            }
            if(false == bFindSame){arrMatch[i] = 0;}
            if(arrMatch[iPos] < arrMatch[i])
            {
                iPos = i;
            }
        }
        TADD_FUNC("Finish.");
        return 0 == arrMatch[iPos]?NULL:&(m_arrTableIndex[iPos]); 
    }*/
    /******************************************************************************
    * 函数名称	:  CombineCMPIndex
    * 函数描述	:  拼接组合索引
    * 输入		:  stLeftIndexValue,stRightIndexValue待组合
    * 输入		:  
    * 输出		:  
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    /*bool TMdbHashIndexCtrl::CombineCMPIndex(ST_INDEX_VALUE & stLeftIndexValue,ST_INDEX_VALUE & stRightIndexValue,
                                    ST_INDEX_VALUE & stOutIndexValue)
    {
        std::map<int ,ST_EXPR *> mapColumnExpr;
        int i = 0;
        for(i = 0;i < MAX_INDEX_COLUMN_COUNTS;++i)
        {
            int iPos = -1;
            if(NULL != stLeftIndexValue.pExprArr[i])
            {
                iPos = stLeftIndexValue.pstTableIndex->pIndexInfo->iColumnNo[i];
                if(iPos < 0  || mapColumnExpr.find(iPos) != mapColumnExpr.end())
                {//查找是否存在
                    return false;
                }
                mapColumnExpr[iPos] = stLeftIndexValue.pExprArr[i];
            }
            if(NULL != stRightIndexValue.pExprArr[i])
            {
                 iPos = stRightIndexValue.pstTableIndex->pIndexInfo->iColumnNo[i];
                if(iPos < 0  || mapColumnExpr.find(iPos) != mapColumnExpr.end())
                {//查找是否存在
                    return false;
                }
                mapColumnExpr[iPos] = stRightIndexValue.pExprArr[i];
            }
        }
        bool bFind = false;
        std::map<int,int> mapColumnToPos;//列对于与值的哪列
        //遍历该表的所有索引，查找包含这些列的索引 
        for(i = 0; i < m_pAttachTable->iIndexCounts; i++)
        {
            TMdbIndex * pMdbIndex = m_arrTableIndex[i].pIndexInfo;
            std::map<int ,ST_EXPR *>::iterator itor = mapColumnExpr.begin();
            mapColumnToPos.clear();
            for(;itor != mapColumnExpr.end();++itor)
            {
                int iColumnPos= itor->first;
                int j = 0;
                bFind = false;
                for(j = 0; j<MAX_INDEX_COLUMN_COUNTS; j++)
                {
                    if(iColumnPos == pMdbIndex->iColumnNo[j])
                    {
                        mapColumnToPos[iColumnPos] = j;
                        bFind = true;
                        break;
                    }
                }
                if(false == bFind ){break;}//有一个不匹配
            }
            if(true == bFind){break;}//都匹配
        }
        
        if(true == bFind && i <  m_pAttachTable->iIndexCounts)
        {//找到
            std::map<int ,ST_EXPR *>::iterator itor = mapColumnExpr.begin();
            stOutIndexValue.pstTableIndex = &m_arrTableIndex[i];
            for(;itor != mapColumnExpr.end();++itor)
            {
                if(mapColumnToPos.end() == mapColumnToPos.find(itor->first))
                {
                    TADD_ERROR(ERROR_UNKNOWN,"not find[%d] in mapColumnToPos",itor->first);
                    return false;
                }
                stOutIndexValue.pExprArr[mapColumnToPos[itor->first]] = itor->second;
            }
            return true;
        }
        else
        {//没有找打
            return false;
        }
        return false;
    }*/

    /******************************************************************************
    * 函数名称	:  PrintWarnIndexInfo
    * 函数描述	: 打印存在冲突链大于指定长度的索引
    * 输入		:  iMaxCNodeCount 最大冲突链长度，存在大于该长度冲突链的索引会被打印
    * 输出		:  
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jiang.lili
    *******************************************************************************/
    int TMdbHashIndexCtrl::PrintWarnIndexInfo(int iMaxCNodeCount)
    {
        int iRet = 0;
        TADD_FUNC("PrintIndexInfo(%d): Start.", iMaxCNodeCount);

        //遍历所有的基础索引块
        TMdbBaseIndexMgrInfo *pBIndexMgr = NULL;//基础索引管理区
        TMdbConflictIndexMgrInfo *pCIndexMgr = NULL;//冲突索引管理区
        TMdbBaseIndex *pBaseIndex = NULL;//基础索引指针
        TMdbConflictIndex *pConfIndex = NULL;//冲突索引指针
        char * pBaseIndexAddr = NULL;//基础索引区地址
        char * pConflictIndexAddr = NULL;//冲突索引区地址
        TMdbIndexNode *pBaseIndexNode = NULL;//基础索引链
        TMdbIndexNode *pConfIndexNode = NULL;//冲突索引链       
		TMdbReIndexNode* pReConfNode = NULL;
        int iBICounts = 0;//基础索引节点数
        int iCICounts = 0;//冲突索引节点数
        int iConflictIndexPos = 0;//冲突索引序号
        int iIndexNodeCount = 0;//索引链上的节点数
        int iIndexLinkCount = 0;//超长的冲突索引链个数
        bool bExist = false;//是否存在超长的索引

        for(int n=0; n<MAX_SHM_ID; ++n)
        {
            pBaseIndexAddr = m_pMdbShmDsn->GetBaseIndex(n);
            if(pBaseIndexAddr == NULL)
            {
                continue;
            }

            pBIndexMgr = (TMdbBaseIndexMgrInfo*)pBaseIndexAddr;//获取基础索引内容
     
            for (int i = 0; i < MAX_BASE_INDEX_COUNTS; i++)
            {
                pBaseIndex = &pBIndexMgr->tIndex[i];
                TMdbTable* pTable = m_pMdbShmDsn->GetTableByName(pBaseIndex->sTabName);
                if (pTable->bIsSysTab|| pBaseIndex->cState == '0')
                {
                    continue;
                }
                pConflictIndexAddr = m_pMdbShmDsn->GetConflictIndex(pBaseIndex->iConflictMgrPos);//获取对应的冲突索引内容
                pCIndexMgr  = (TMdbConflictIndexMgrInfo *)pConflictIndexAddr;
                CHECK_OBJ(pCIndexMgr);

                iBICounts = pBaseIndex->iSize/sizeof(TMdbIndexNode);//基础索引个数
                iConflictIndexPos = pBaseIndex->iConflictIndexPos;
                pConfIndex = &pCIndexMgr->tIndex[iConflictIndexPos];

				if(pConfIndex->bRePos)
	            {
	                iCICounts =pConfIndex->iSize/sizeof(TMdbReIndexNode);//冲突索引个数
	                pReConfNode = (TMdbReIndexNode*)(pConflictIndexAddr + pConfIndex->iPosAdd);//冲突索引连
	            }
				else
		        {
		            iCICounts =pConfIndex->iSize/sizeof(TMdbIndexNode);//冲突索引个数
		            pConfIndexNode = (TMdbIndexNode*)(pConflictIndexAddr + pConfIndex->iPosAdd);//冲突索引连
		        }
				 

                pBaseIndexNode = (TMdbIndexNode*)(pBaseIndexAddr + pBaseIndex->iPosAdd);//基础索引链

                iIndexLinkCount = 0;
                for(int j = 0; j < iBICounts; j++)
                {//遍历每条链               
                    int iNextPos = pBaseIndexNode[j].iNextPos;
                    iIndexNodeCount = 0;
                    while(iNextPos >= 0)
                    {
                        iIndexNodeCount++;
                        if(iIndexNodeCount > iCICounts)
                        {
                            OutPutInfo(true,"\nOMG,unlimited loop...\n");
                            return 0;
                        }
						if(pConfIndex->bRePos)
	                    {
	                        iNextPos = pReConfNode[iNextPos].iNextPos;
	                    }
						else
						{
                        	iNextPos = pConfIndexNode[iNextPos].iNextPos;
						}
                    }
                    if (iIndexNodeCount > iMaxCNodeCount)
                    {
                        iIndexLinkCount++;
                    }                 
                }
                if (iIndexLinkCount > 0)
                {
                    if (!bExist)
                    {
                        bExist = true;
                        OutPutInfo(true, "Index link longer than %d: \n\n", iMaxCNodeCount);
                    }
                    OutPutInfo(true, "    TABLE = [ %s], INDEX_NAME = [ %s ], LINK_COUNT = [ %d ]\n",  
                        pBaseIndex->sTabName, pBaseIndex->sName, iIndexLinkCount);
                }          
            }
        }

        if (!bExist)
        {
            OutPutInfo(true, "All the Indexes are OK. No conflict index link longer than %d.\n ", iMaxCNodeCount);
        }

        TADD_FUNC("PrintIndexInfo(%d): Finish.", iMaxCNodeCount);
        return iRet;
    }

    /******************************************************************************
    * 函数名称	:  RebuildTableIndex
    * 函数描述	:  重新构建某表索引区
    * 输入		:  
    * 输入		:  
    * 输出		:  
    * 返回值	:  0 - 成功!0 -失败
    * 作者		:  jin.shaohua
    *******************************************************************************/
    /*int TMdbHashIndexCtrl::RebuildTableIndex(bool bNeedToClean)
    {
        TADD_FUNC("Start");
        int iRet = 0;
        CHECK_OBJ(m_pAttachTable);
        if(bNeedToClean)
        {
            //TODO:需要清理老的索引区
        }
        
        TADD_FUNC("Finish.");
        return iRet;
    }*/

    
//}


