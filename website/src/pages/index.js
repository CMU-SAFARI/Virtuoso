import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import HomepageFeatures from '@site/src/components/HomepageFeatures';
import Heading from '@theme/Heading';
import styles from './index.module.css';

function HomepageHeader() {
  const { siteConfig } = useDocusaurusContext();
  return (
    <header className={clsx('hero hero--primary', styles.heroBanner)}>
      <div className="container">
        <div className={styles['hero-content-container']} style={{ color: 'white' }}>
          <div className={styles['hero-text-area']} style={{ width: '80%', color: 'white' }}>
            <Heading as="h1" className="hero__title" style={{ color: '#295185', fontSize: '4.5rem' }}>
              Virtuoso
            </Heading>
            <p style={{color: 'black', fontSize: '2.5rem', marginBottom: '1rem', textAlign : 'left'}}>
              Fast and Accurate Virtual Memory Research
            </p>
            <p style={{color: 'black', fontSize:'1.6rem', fontStyle:'italic'}}>
              A new simulation framework for fast and accurate prototyping of virtual memory designs through an imitation-based OS methodology
            </p>
          </div>
          <div className={styles['hero-image-area']}>
            <img 
              src={require('@site/static/img/virtuoso_logo.png').default}
              alt="Virtuoso" 
              style={{ transform: 'scale(0.9)' }} 
            />
          </div>
        </div>
      </div>

    </header>
  );
}

export default function Home() {
  return (
    <Layout
      title="Virtuoso"
      description="Virtuoso: A fast and accurate VM simulation framework via Imitation-based OS simulation.">
      <HomepageHeader />

      <main>
        <HomepageFeatures />
      </main>
    </Layout>
  );
}