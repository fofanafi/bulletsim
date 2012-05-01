#include "cloth.h"

Cloth::Cloth(int resx_, int resy_,
        btScalar lenx_, btScalar leny_,
        const btVector3 &initCenter,
        btSoftBodyWorldInfo &worldInfo) :
            resx(resx_), resy(resy_),
            lenx(lenx_), leny(leny_),
            BulletSoftObject(
                    makeSelfCollidingTowel(
                        initCenter,
                        lenx_, leny_,
                        resx_, resy_, worldInfo)) {
    initAccel();
}

Cloth::Cloth(int resx_, int resy_,
        btScalar lenx_, btScalar leny_,
        BulletSoftObject::Ptr sb) :
            resx(resx_), resy(resy_),
            lenx(lenx_), leny(leny_),
            BulletSoftObject(*sb) {
    initAccel();
}

EnvironmentObject::Ptr Cloth::copy(Fork &f) const {
    BulletSoftObject::Ptr sb = boost::static_pointer_cast<BulletSoftObject>(BulletSoftObject::copy(f));
    return Ptr(new Cloth(resx, resy, lenx, leny, sb));
}

void Cloth::initAccel() {
    cloud.reset(new pcl::PointCloud<pcl::PointXYZ>());
    kdtree.reset(new pcl::KdTreeFLANN<pcl::PointXYZ>());
    cloud->points.resize(resx * resy);
    kdtree->setInputCloud(cloud);
}

void Cloth::updateAccel() {
    // fill in cloud with cloth points
    BOOST_ASSERT(cloud->points.size() == resx * resy);
    for (int i = 0; i < cloud->points.size(); ++i) {
        const btVector3 &p = psb()->m_nodes[i].m_x;
        cloud->points[i].x = p.x();
        cloud->points[i].y = p.y();
        cloud->points[i].z = p.z();
    }

    // update kdtree
    kdtree->setInputCloud(cloud);
}

btVector3 Cloth::centerPoint() const {
    btVector3 centerpt(0, 0, 0);
    for (int i = 0; i < psb()->m_nodes.size(); ++i)
        centerpt += psb()->m_nodes[i].m_x;
    centerpt /= psb()->m_nodes.size();
    return centerpt;
}

void Cloth::translateRel(const btTransform &t, const btVector3 &pt) {
    btTransform ctr(btQuaternion::getIdentity(), pt);
    btTransform conj = ctr * t * ctr.inverse();

    const btScalar margin=psb()->getCollisionShape()->getMargin();
    ATTRIBUTE_ALIGNED16(btDbvtVolume) vol;
    for(int i=0,ni=psb()->m_nodes.size();i<ni;++i) {
        btSoftBody::Node &n=psb()->m_nodes[i];
        n.m_x=conj*n.m_x;
        n.m_q=conj*n.m_q;
        n.m_n=conj.getBasis()*n.m_n;
        vol = btDbvtVolume::FromCR(n.m_x,margin);
        psb()->m_ndbvt.update(n.m_leaf,vol);
	}
    psb()->updateNormals();
    psb()->updateBounds();
    psb()->updateConstants();
}

void Cloth::translateCenterToPt(const btVector3 &pt) {
    translateRelToCenter(btTransform(btQuaternion::getIdentity(),
                pt - centerPoint()));
}
