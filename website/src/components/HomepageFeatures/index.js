import clsx from 'clsx';
import Heading from '@theme/Heading';
import styles from './styles.module.css';

const FeatureList = [
  {
    title: 'Fast & Lightweight',
    Svg: require('@site/static/img/rocket-svgrepo-com.svg').default, 
      description: (
      <>
        Virtuoso employs <strong>MimicOS</strong>, a lightweight userspace kernel that imitates only
        the required OS routines. This significantly speeds up simulation time than full-system Linux simulation.
      </>
    ),
  },
  {
    title: 'Accurate',
    Svg: require('@site/static/img/accuracy-aim-competition-svgrepo-com.svg').default,
    description: (
      <>
        Virtuoso improves the accuracy of modeling the virtual memory subsystem. 
        This way it improves performance estimation accuracy compared to traditional simulation methods.
      </>
    ),
  },
  {
    title: 'Modular & Versatile',
    Svg: require('@site/static/img/automate-modular-management-svgrepo-com.svg').default,
    description: (
      <>
        Virtuoso is designed with  versatility in mind. You can easily extend it to evaluate new
        virtual memory schemes by integrating with diverse simulators like
        Sniper, gem5, Ramulator, and ChampSim. 
      </>
    ),
  },
];

function Feature({Svg, title, description}) {
  return (
    <div className={clsx('col col--4')}>
      <div className="text--center">
        <Svg className={styles.featureSvg} role="img" />
      </div>
      <div className="text--center padding-horiz--md">
        <Heading as="h3">{title}</Heading>
        <p>{description}</p>
      </div>
    </div>
  );
}

export default function HomepageFeatures() {
  return (
    <section className={styles.features}>
      <div className="container-fluid">
        <div className="row">
          {FeatureList.map((props, idx) => (
            <Feature key={idx} {...props} />
          ))}
        </div>

        {/* Get Started Button */}
        <div style={{ margin: '5em', textAlign: "center" }}>
          <a className="myButton" href="/docs/intro">Get Started</a>
        </div>

        {/* Citation Block */}
        <div className="row" style={{ justifyContent: 'center', padding: '1rem' }}>
          <div
            style={{
              backgroundColor: '#e9eeef',
              padding: '2rem',
              width: '100%',
              maxWidth: '900px',
              maxHeight: '400px',
              overflowY: 'auto',
              borderRadius: '8px',
              boxShadow: '0 0 10px rgba(0,0,0,0.1)'
            }}
          >
            <h2 style={{ textAlign: 'center', fontSize: '2.5rem', marginBottom: '1rem' }}>Citation</h2>
            <div className="row" style={{ justifyContent: 'center', padding: '1rem' }}>
            <div
                  style={{
                    backgroundColor: '#e9eeef',
                    padding: '2rem',
                    width: '100%',
                    maxWidth: '900px',
                    maxHeight: '400px',
                    overflowY: 'auto',
                    borderRadius: '8px',
                    boxShadow: '0 0 10px rgba(0,0,0,0.1)',
                    fontFamily: 'monospace',
                  }}
                >

                  <pre>
                    <code>
              {`@inproceedinds{kanellopoulos2025virtuoso,
                title     = {Virtuoso: Enabling Fast and Accurate Virtual Memory Research via an Imitation-based Operating System Simulation Methodology},
                author    = {Konstantinos Kanellopoulos and Konstantinos Sgouras and F. Nisa Bostanci and Andreas Kosmas Kakolyris and Berkin Kerim Konar and Rahul Bera and Mohammad Sadrosadati and Rakesh Kumar and Nandita Vijaykumar and Onur Mutlu},
                year      = {2025},
                booktitle = {ASPLOS}
              }`}
                    </code>
                  </pre>
                </div>
              </div>
            </div>
          </div>
        </div>
    </section>
  );
}
